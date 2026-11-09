"""Bootstrap-only semantic checks and executable contract behavior."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

BINARY = Path(sys.argv[1]).resolve()

INVALID = (
    ("integer_match_duplicate", "proc f(x: int) -> int { match x { 1 => 1, 0x1 => 2, _ => 0, } }", ("E1602",)),
    ("string_match_duplicate", 'proc f(x: string) -> int { match x { "same" => 1, "same" => 2, _ => 0, } }', ("E1602",)),
    ("match_false_guard", "proc f(x: int) -> int { match x { 1 if 2 < 1 => 1, _ => 0, } }", ("E1602",)),
    ("bool_match_postcondition", "proc f(x: bool) -> bool ensures result; { match x { true => true, false => false, } }", ("E0702",)),
    ("unknown_unary_no_cascade", "proc f() -> bool { return not missing; }", ("E0401",)),
    ("unknown_index_no_cascade", "proc f() -> int { let a = [1]; return a[missing]; }", ("E0401",)),
    ("unknown_array_no_cascade", "proc f() -> int { return missing[0]; }", ("E0401",)),
    ("match_literal_type", "proc f(x: bool) -> int { match x { 1 => 1, _ => 0, } }", ("E1603",)),
    ("match_postcondition", "proc f(x: bool) -> int ensures result > 0; { match x { true => 1, false => -1, } }", ("E0702",)),
    ("signed_literal_overflow", "proc f() -> int { return 9223372036854775808; }", ("E1502",)),
    ("constant_cycle", "const A: int = A + 1; proc main() -> int { return 0; }", ("E1504",)),
    ("divide_unknown_numerator", "proc f(x: int) -> int { return x / 0; }", ("E1503",)),
    ("divide_named_zero", "const ZERO: int = 0; proc f() -> int { return 1 / ZERO; }", ("E1503",)),
    ("multiply_overflow", "proc f() -> int { return 9223372036854775807 * 2; }", ("E1502",)),
    ("subtract_overflow", "proc f() -> int { return -9223372036854775808 - 1; }", ("E1502",)),
    ("divide_overflow", "proc f() -> int { return -9223372036854775808 / -1; }", ("E1502",)),
    ("negate_overflow", "proc f() -> int { return -(-9223372036854775808); }", ("E1502",)),
    ("unsigned_overflow", "const TOP: u64 = 18446744073709551615; proc f() -> u64 { return TOP + 1; }", ("E1502",)),
    ("negative_shift", "proc f() -> int { return 1 << -1; }", ("E0512",)),
    ("forward_precondition", "proc main() -> int { return positive(0); } proc positive(x: int) -> int requires x > 0; { return x; }", ("E0701",)),
    ("folded_precondition", "proc f(x: int) -> int requires x > 0; { return x; } proc main() -> int { let x = 1 - 2; return f(x); }", ("E0701",)),
    ("folded_postcondition", "proc f() -> int ensures result > 0; { let x = 1 - 2; return x; }", ("E0702",)),
    ("computed_contradiction", "proc f() -> int requires not (2 < 3); { return 0; }", ("E0709",)),
    ("while_missing_return", "proc f(x: bool) -> int { while x { return 1; } }", ("E1101",)),
    ("false_loop_missing_return", "proc f() -> int { while false { return 1; } }", ("E1101",)),
    ("while_break_missing_return", "proc f() -> int { while true { break; } }", ("E1101",)),
    ("unsafe_missing_return", "proc f() -> int { unsafe { let x = 1; } }", ("E1101",)),
    ("bool_match_missing_arm", "proc f(x: bool) -> int { match x { true => { return 1; }, } }", ("E1601",)),
    ("bool_match_duplicate", "proc f(x: bool) -> int { match x { true => { return 1; }, true => { return 2; }, false => { return 0; }, } }", ("E1602",)),
    ("wildcard_match_unreachable", "proc f(x: int) -> int { match x { _ => { return 0; }, 1 => { return 1; }, } }", ("E1602",)),
    ("match_guard_type", "proc f(x: bool) -> int { match x { true if 1 => { return 1; }, _ => { return 0; }, } }", ("E0515",)),
    ("match_implicit_return_type", 'proc f(x: bool) -> int { match x { true => "bad", false => 0, } }', ("E0605",)),
    ("contract_await_effect", "async proc ready() -> bool { return true; } async proc f() -> int requires await ready(); { return 0; }", ("E0710",)),
)

VALID = (
    ("guarded_integer_fallback", "proc f(x: int) -> int { match x { 1 if x > 2 => 1, 1 => 2, _ => 0, } }"),
    ("distinct_integer_patterns", "proc f(x: int) -> int { match x { 1 => 1, 2 => 2, _ => 0, } }"),
    ("distinct_string_patterns", 'proc f(x: string) -> int { match x { "one" => 1, "two" => 2, _ => 0, } }'),
    ("signed_minimum", "proc f() -> int { return -9223372036854775808; }"),
    ("unsigned_maximum", "proc f() -> u64 { return 18446744073709551615; }"),
    ("unsigned_maximum_cast", "proc f() -> u64 { return 18446744073709551615 as u64; }"),
    ("arithmetic_boundary", "proc f() -> int { return 9223372036854775807 - 1; }"),
    ("unknown_runtime_denominator", "proc f(x: int) -> int { return 1 / x; }"),
    ("short_circuit_and", "proc f() -> bool { return false and (1 / 0 == 0); }"),
    ("short_circuit_or", "proc f() -> bool { return true or (1 / 0 == 0); }"),
    ("dead_branch_arithmetic", "proc f() -> int { if false { return 1 / 0; } return 0; }"),
    ("dead_branch_postcondition", "proc f() -> int ensures result > 0; { if false { return -1; } return 1; }"),
    ("computed_condition_return", "proc f() -> int { if 1 < 2 { return 1; } }"),
    ("infinite_loop", "proc f() -> int { loop { } }"),
    ("infinite_while", "proc f() -> int { while true { } }"),
    ("nested_loop_break", "proc f() -> int { loop { loop { break; } } }"),
    ("loop_return", "proc f() -> int { loop { return 1; } }"),
    ("safe_array_index", "proc f() -> int { let a = [1, 2]; return a[1]; }"),
    ("unknown_array_index", "proc f(x: int) -> int { let a = [1, 2]; return a[x]; }"),
    ("bool_match_exhaustive", "proc f(x: bool) -> int { match x { true => { return 1; }, false => { return 0; }, } }"),
    ("bool_match_guard_fallback", "proc f(x: bool) -> int { match x { true if x => { return 1; }, _ => { return 0; }, } }"),
    ("unknown_precondition", "proc positive(x: int) -> int requires x > 0; { return x; } proc f(x: int) -> int { return positive(x); }"),
    ("mutable_value_not_folded", "proc positive(x: int) -> int requires x > 0; { return x; } proc f() -> int { let mut x = -1; set x = 1; return positive(x); }"),
)

RUNTIME = (
    ("valid_contract", "proc positive(x: int) -> int requires x > 0; ensures result > 0; { return x; } proc main() -> int { return positive(1) - 1; }", None, ""),
    ("runtime_requires", 'proc positive(x: int) -> int requires x > 0; { print("body"); return x; } proc main() -> int { let mut x = -1; return positive(x); }', "E0701", ""),
    ("runtime_ensures", "proc f(x: int) -> int ensures result > 0; { return x; } proc main() -> int { let mut x = -1; return f(x); }", "E0702", ""),
    ("snapshot_shadow", "proc f(x: int) -> int ensures result == x; { let x = 9; return 1; } proc main() -> int { return f(1) - 1; }", None, ""),
    ("nested_return", "proc f(x: int) -> int ensures result > 0; { if x < 0 { return x; } return 1; } proc main() -> int { let mut x = -1; return f(x); }", "E0702", ""),
    ("result_evaluated_once", "static mut COUNT: int = 0; proc next() -> int { set COUNT = COUNT + 1; return COUNT; } proc f() -> int ensures result > 0; { return next(); } proc main() -> int { let n = f(); return COUNT - n; }", None, ""),
    ("defer_before_postcondition", "static mut COUNT: int = 0; proc f() -> int ensures COUNT == 1; ensures result == 7; { defer { set COUNT = 1; } return 7; } proc main() -> int { return f() - 7; }", None, ""),
    ("void_explicit_postcondition", "static mut COUNT: int = 0; proc f() ensures COUNT > 0; { return; } proc main() -> int { f(); return 0; }", "E0702", ""),
    ("void_implicit_postcondition", "static mut COUNT: int = 0; proc f() ensures COUNT > 0; { } proc main() -> int { f(); return 0; }", "E0702", ""),
    ("void_defer_postcondition", "static mut COUNT: int = 0; proc f() ensures COUNT == 1; { defer { set COUNT = 1; } } proc main() -> int { f(); return COUNT - 1; }", None, ""),
    ("multiple_clauses", "proc f(x: int) -> int requires x > 0; requires x < 10; ensures result > 0; ensures result < 10; { return x; } proc main() -> int { let mut x = 12; return f(x); }", "E0701", ""),
    ("reference_parameter", "proc f(x: &int) -> int requires x > 0; ensures result > 0; { return x; } proc main() -> int { let x = 1; return f(&x) - 1; }", None, ""),
    ("implicit_match_return", "static mut BAD: int = -1; proc f(x: bool) -> int ensures result > 0; { match x { true => 1, false => BAD, } } proc main() -> int { let mut x = false; return f(x); }", "E0702", ""),
    ("async_contract", "async proc f(x: int) -> int requires x > 0; ensures result > 0; { return x; } proc main() -> int { let mut x = -1; let task = f(x); return await task; }", "E0701", ""),
    ("generic_contract", "proc f<T>(x: T, valid: bool) -> T requires valid; ensures valid; { return x; } proc main() -> int { let mut valid = false; return f(1, valid); }", "E0701", ""),
)


def check_source(directory, name, source, expected):
    path = directory / (name + ".vit")
    path.write_text(source, encoding="utf-8")
    result = subprocess.run([str(BINARY), "compile", str(path), "--stop-after-sema", "--quiet", "--no-color", "--diagnostic=json"], capture_output=True, text=True, timeout=20)
    document = json.loads(result.stderr) if result.stderr else {"diagnostics": []}
    diagnostics = document["diagnostics"]
    codes = tuple(item["code"] for item in diagnostics if item["severity"] in ("error", "fatal"))
    assert codes == expected, (name, expected, result.returncode, result.stderr)
    assert result.returncode == bool(expected), (name, result.returncode, result.stderr)
    if name in ("integer_match_duplicate", "string_match_duplicate"):
        assert "already covers this value" in result.stderr, (name, result.stderr)
    if name == "match_false_guard":
        assert "guard is always false" in result.stderr, (name, result.stderr)
    for item in diagnostics:
        assert item["position"]["has_span"], (name, item)
        assert any(annotation["kind"] == "help" and annotation["message"] for annotation in item["annotations"]), (name, item)
    return path


with tempfile.TemporaryDirectory(prefix="vitte-bootstrap-semantics-") as temporary:
    directory = Path(temporary)
    for name, source, codes in INVALID:
        check_source(directory, name, source, codes)
    for name, source in VALID:
        check_source(directory, name, source, ())
    for name, source, code, stdout in RUNTIME:
        path = check_source(directory, name, source, ())
        executable = directory / (name + ".exe")
        compiled = subprocess.run([str(BINARY), "compile", str(path), "-o", str(executable), "--quiet", "--no-color"], capture_output=True, text=True, timeout=30)
        assert compiled.returncode == 0, (name, compiled.stdout, compiled.stderr)
        ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
        assert ran.returncode == (1 if code else 0), (name, ran.returncode, ran.stdout, ran.stderr)
        assert ran.stdout == stdout, (name, ran.stdout)
        if code:
            assert ran.stderr.count(f"error[{code}]") == 1, (name, ran.stderr)
            assert "phase: contract" in ran.stderr and "contract:" in ran.stderr, (name, ran.stderr)
            assert "  help: " in ran.stderr, (name, ran.stderr)
            assert str(path) in ran.stderr, (name, ran.stderr)
        else:
            assert not ran.stderr, (name, ran.stderr)

print(f"Bootstrap semantics passed: {len(INVALID)} invalid, {len(VALID)} valid, {len(RUNTIME)} executable contract cases.")
