"""Pin the public contract of errors emitted by the C bootstrap frontend."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[2]

# Each span is the exact source fragment that the compiler must underline.
# Byte offsets and human-readable line/column coordinates are checked
# separately, including a multibyte UTF-8 character before a later error.
CASES = (
    ("requires-not-bool", "compile",
     "proc f(x: int) -> int requires x + 1; { return x; }",
     ("E0704",), "contract", "x + 1"),
    ("ensures-not-bool", "compile",
     "proc f(x: int) -> int ensures result + 1; { return x; }",
     ("E0705",), "contract", "result + 1"),
    ("requires-false", "compile",
     "proc f() -> int requires false; { return 0; }",
     ("E0709",), "contract", "false"),
    ("ensures-false", "compile",
     "proc f() -> int ensures false; { return 0; }",
     ("E0702",), "contract", "false"),
    ("requires-result", "compile",
     "proc f() -> int requires result > 0; { return 1; }",
     ("E0707",), "contract", "result"),
    ("ensures-void-result", "compile",
     "proc f() ensures result > 0; { }",
     ("E0707",), "contract", "result"),
    ("multiple-contract-errors", "compile",
     'proc f() -> int requires 1; ensures 2; { return "bad"; }',
     ("E0704", "E0705", "E0605"), "contract", "1",
     {"E0705": "2", "E0605": 'return "bad";'}),
    ("unreachable-after-branches", "compile",
     "proc f(x: bool) -> int { if x { return 1; } else { return 2; } let y = 3; }",
     ("W0020",), "type-check", "let y = 3;"),
    ("missing-return-empty", "compile",
     "proc f() -> int { }", ("E1101",), "type-check", "}"),
    ("missing-return-local", "compile",
     "proc f() -> int { let x = 1; }", ("E1101",), "type-check", "}"),
    ("missing-return-branch", "compile",
     "proc f(x: bool) -> int { if x { return 1; } }",
     ("E1101",), "type-check", ("}", 1)),
    ("constant-divide-zero", "compile",
     "proc main() -> int { return 8 / (2 - 2); }",
     ("E1503",), "constant-eval", "8 / (2 - 2)"),
    ("constant-remainder-zero", "compile",
     "proc main() -> int { return 8 % 0; }",
     ("E1503",), "constant-eval", "8 % 0"),
    ("constant-overflow", "compile",
     "proc main() -> int { return 9223372036854775807 + 1; }",
     ("E1502",), "constant-eval", "9223372036854775807 + 1"),
    ("constant-invalid-shift", "compile",
     "proc main() -> int { return 1 << 64; }",
     ("E0512",), "type-check", "1 << 64"),
    ("array-index-out-of-bounds", "compile",
     "proc main() -> int { let a = [1, 2]; return a[2]; }",
     ("E1201",), "type-check", "a[2]"),
    ("array-negative-index", "compile",
     "proc main() -> int { let a = [1, 2]; return a[-1]; }",
     ("E1201",), "type-check", "a[-1]"),
    ("call-precondition", "compile",
     "proc f(x: int) -> int requires x > 0; { return x; } "
     "proc main() -> int { return f(-1); }",
     ("E0701",), "contract", "f(-1)"),
    ("return-postcondition", "compile",
     "proc f() -> int ensures result > 0; { return -1; }",
     ("E0702",), "contract", "return -1;"),
    ("requires-computed-false", "compile",
     "proc f() -> int requires 2 > 3; { return 0; }",
     ("E0709",), "contract", "2 > 3"),
    ("missing-return-loop-break", "compile",
     "proc f() -> int { loop { break; } }",
     ("E1101",), "type-check", ("}", 1)),
    ("bool-match-missing-arm", "compile",
     "proc f(x: bool) -> int { match x { true => { return 1; }, } }",
     ("E1601",), "type-check", "match x { true => { return 1; }, }"),
    ("bool-match-unreachable", "compile",
     "proc f(x: bool) -> int { match x { _ => { return 0; }, true => { return 1; }, } }",
     ("E1602",), "type-check", "true"),
    ("contract-await-effect", "compile",
     "async proc ready() -> bool { return true; } "
     "async proc f() -> int requires await ready(); { return 0; }",
     ("E0710",), "contract", "await ready()"),
    ("constant-self-cycle", "compile",
     "const VALUE: int = VALUE; proc main() -> int { return 0; }",
     ("E1504",), "constant-eval", "const VALUE: int = VALUE;"),
    ("match-pattern-type", "compile",
     "proc f(x: bool) -> int { match x { 1 => 1, _ => 0, } }",
     ("E1603",), "type-check", ("1", 0)),
    ("lexer-invalid-character", "lex",
     "proc main() -> int { let x = 'a; return 0; }", ("E0101",), "lexer", "'a"),
    ("lexer-unterminated-string", "check",
     'proc main() -> int { let x = "unfinished; return 0; }',
     ("E0102",), "lexer", '"unfinished; return 0; }'),
    ("lexer-invalid-escape", "check",
     'proc main() -> int { let x = "bad\\q"; return 0; }',
     ("E0103",), "lexer", "\\q"),
    ("lexer-invalid-number", "check",
     "proc main() -> int { let x = 123abc; return 0; }",
     ("E0104",), "lexer", "123a"),
    ("lexer-integer-overflow", "lex",
     "proc main() -> int { let x = 18446744073709551616; return 0; }",
     ("E0105",), "lexer", "18446744073709551616"),
    ("lexer-unterminated-comment", "lex",
     "proc main() -> int { /* never closed", ("E0106",), "lexer", "/* never closed"),
    ("parser-expected-type", "check",
     "proc main() -> int { let x: = 1; return 0; }",
     ("E0201",), "parser", "="),
    ("parser-expected-expression", "check",
     "proc main() -> int { let x = ; return 0; }",
     ("E0203",), "parser", ";"),
    ("parser-expected-declaration", "check",
     "return 1;", ("E0204",), "parser", "return"),
    ("missing-module", "compile",
     "space contract::main;\nuse contract::absent;\n"
     "proc main() -> int { return 0; }\n",
     ("E0301",), "import", "use contract::absent;"),
    ("unknown-name", "compile",
     "proc main() -> int { return missing; }", ("E0401",), "name-resolution", "missing"),
    ("duplicate-declaration", "compile",
     "proc main() -> int { let x = 1; let x = 2; return x; }",
     ("E0402",), "name-resolution", "let x = 2;"),
    ("shadowed-global", "compile",
     "const VALUE: i64 = 1;\n"
     "proc main(VALUE: i64) -> i64 { return VALUE; }\n",
     ("W0030",), "name-resolution", ("VALUE: i64", 1)),
    ("unknown-member", "compile",
     "form Item { value: int; } proc main(item: Item) -> int { return item.missing; }",
     ("E0506",), "type-check", "missing"),
    ("local-type-mismatch", "compile",
     'proc main() -> int { let x: int = "bad"; return 0; }',
     ("E0501",), "type-check", 'let x: int = "bad";'),
    ("not-callable", "compile",
     "proc main() -> int { let x = 1; return x(); }", ("E0504",), "type-check", "x()"),
    ("not-indexable", "compile",
     "proc main() -> int { return 1[0]; }", ("E0505",), "type-check", "1[0]"),
    ("unknown-type", "compile",
     "proc main(x: Missing) -> int { return 0; }", ("E0510",), "type-check", "Missing"),
    ("not-a-type", "compile",
     "const Value: int = 1; proc main(x: Value) -> int { return 0; }",
     ("E0510", "E0511"), "type-check", ("Value", 1)),
    ("invalid-operands", "compile",
     "proc main() -> int { if 1 && 2 { return 1; } return 0; }",
     ("E0512",), "type-check", "1 && 2"),
    ("not-assignable", "compile",
     "proc main() -> int { let x = 1; set x = 2; return x; }",
     ("E0513",), "type-check", "x = 2"),
    ("invalid-index-type", "compile",
     'proc main() -> int { let x = [1, 2]; return x["x"]; }',
     ("E0514",), "type-check", 'x["x"]'),
    ("condition-not-bool", "compile",
     "proc main() -> int { if 1 { return 1; } return 0; }",
     ("E0515",), "type-check", "1"),
    ("call-arity", "compile",
     "proc f(x: int) -> int { return x; } proc main() -> int { return f(); }",
     ("E0601",), "type-check", "f()"),
    ("call-argument-type", "compile",
     'proc f(x: int) -> int { return x; } proc main() -> int { return f("x"); }',
     ("E0602",), "type-check", '"x"'),
    ("return-type", "compile",
     'proc main() -> int { return "x"; }', ("E0605",), "type-check", 'return "x";'),
    ("return-outside-procedure", "compile",
     'test "return outside procedure" { return 0; }',
     ("E0607",), "type-check", "return 0;"),
    ("utf8-before-error", "compile",
     'proc main() -> int {\n    let label = "é";\n    return "bad";\n}',
     ("E0605",), "type-check", 'return "bad";'),
    ("break-outside-loop", "compile",
     "proc main() -> int { break; return 0; }", ("E1102", "W0020"), "type-check", "break;",
     {"W0020": "return 0;"}),
    ("continue-outside-loop", "compile",
     "proc main() -> int { continue; return 0; }",
     ("E1103", "W0020"), "type-check", "continue;", {"W0020": "return 0;"}),
    ("unreachable-code-warning", "compile",
     "proc main() -> int { return 1; let x = 2; }",
     ("W0020",), "type-check", "let x = 2;"),
)

TYPE_CONTEXTS = {
    "requires-not-bool": ("bool", "i64", "the contract expression must be valid and have type bool"),
    "ensures-not-bool": ("bool", "i64", "the contract expression must be valid and have type bool"),
    "local-type-mismatch": ("i64", "string", "the expected and found types are not assignable"),
    "invalid-index-type": ("i64", "string", ""),
    "condition-not-bool": ("bool", "i64", "the condition must be boolean"),
    "call-argument-type": ("i64", "string", "the argument does not satisfy the parameter type"),
    "return-type": ("i64", "string", "the returned value does not satisfy the declared return type"),
}

# This inventory is the executable source-diagnostic surface of the C
# bootstrap frontend today. E0108 is exercised through the public C API in
# diagnostic_render_test.c because the CLI deliberately reports malformed
# files as the file-level E0006. Keeping this inventory explicit means a new
# source diagnostic cannot silently arrive without an exact source witness.
ACTIVE_BOOTSTRAP_SOURCE_CODES = {
    "E0006",  # malformed file encoding through the CLI
    "E0101", "E0102", "E0103", "E0104", "E0105", "E0106", "E0108",
    "E0201", "E0203", "E0204",
    "E0301", "E0303", "E0304",
    "E0401", "E0402", "E0404",
    "E0501", "E0504", "E0505", "E0506", "E0510", "E0511", "E0512",
    "E0513", "E0514", "E0515",
    "E0601", "E0602", "E0605", "E0607",
    "E0701", "E0702", "E0704", "E0705", "E0707", "E0709", "E0710",
    "E1201", "E1502", "E1503", "E1504", "E1601", "E1602", "E1603",
    "E1101", "E1102", "E1103", "W0020", "W0030",
}


def invoke(path: Path, command: str, output_format: str):
    args = [
        str(binary), command, str(path), "--quiet", "--no-color",
        f"--diagnostic={output_format}",
    ]
    if command == "compile":
        args.append("--stop-after-sema")
    return subprocess.run(args, capture_output=True, text=True)


def check_case(directory: Path, case):
    name, command, source, expected_codes, expected_phase, marked = case[:6]
    additional_spans = case[6] if len(case) > 6 else {}
    path = directory / f"{name}.vit"
    path.write_text(source, encoding="utf-8")
    result = invoke(path, command, "json")
    expects_error = any(code.startswith("E") for code in expected_codes)
    expected_status = 1 if expects_error else 0
    assert result.returncode == expected_status, (
        name,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    assert not result.stdout, (name, result.stdout)
    document = json.loads(result.stderr)
    diagnostics = document["diagnostics"]
    assert tuple(item["code"] for item in diagnostics) == expected_codes, (
        name,
        expected_codes,
        diagnostics,
    )
    for diagnostic in diagnostics:
        expected_severity = "warning" if diagnostic["code"].startswith("W") else "error"
        diagnostic_marked = additional_spans.get(diagnostic["code"], marked)
        marker, occurrence = (
            diagnostic_marked if isinstance(diagnostic_marked, tuple)
            else (diagnostic_marked, 0)
        )
        marker_positions = [match.start() for match in re.finditer(re.escape(marker), source)]
        assert occurrence < len(marker_positions), (name, marker, marker_positions)
        expected_start = len(source[:marker_positions[occurrence]].encode("utf-8"))
        expected_end = expected_start + len(marker.encode("utf-8"))
        expected_start_line = source[:marker_positions[occurrence]].count("\n")
        expected_start_column = len(
            source[:marker_positions[occurrence]].rsplit("\n", 1)[-1]
        )
        marker_end = marker_positions[occurrence] + len(marker)
        expected_end_line = source[:marker_end].count("\n")
        expected_end_column = len(source[:marker_end].rsplit("\n", 1)[-1])
        position = diagnostic["position"]
        assert position["has_span"], (name, diagnostic)
        assert position["source"] == str(path), (name, position)
        assert (position["start_byte"], position["end_byte"]) == (
            expected_start,
            expected_end,
        ), (name, expected_start, expected_end, position)
        assert (position["start_line"], position["start_column"]) == (
            expected_start_line,
            expected_start_column,
        ), (name, expected_start_line, expected_start_column, position)
        assert (position["end_line"], position["end_column"]) == (
            expected_end_line,
            expected_end_column,
        ), (name, expected_end_line, expected_end_column, position)
        phase = "type-check" if name == "multiple-contract-errors" and diagnostic["code"] == "E0605" else expected_phase
        assert diagnostic["phase"] == phase, (name, diagnostic)
        assert diagnostic["severity"] == expected_severity, (name, diagnostic)
        assert diagnostic["internal_code"] and diagnostic["category"], (name, diagnostic)
        assert diagnostic["message"].strip(), (name, diagnostic)
        if name.startswith(("requires-", "ensures-")) or name in ("call-precondition", "return-postcondition"):
            contract = diagnostic["contract"]
            assert contract and contract["procedure"] == "f", (name, contract)
            assert contract["kind"] == ("ensures" if name.startswith("ensures-") or name == "return-postcondition" else "requires"), (name, contract)
            assert contract["expression"] in source, (name, contract)
            assert diagnostic["context"]["procedure"] == "f", (name, diagnostic)
        if name in TYPE_CONTEXTS:
            expected, actual, cause = TYPE_CONTEXTS[name]
            context = diagnostic["context"]
            assert context["expected_type"] == context["expected"] == expected, (
                name,
                context,
            )
            assert context["actual_type"] == context["found"] == actual, (name, context)
            assert context["probable_cause"] == cause, (name, context)

        primary = [label for label in diagnostic["labels"] if label["style"] == "primary"]
        assert len(primary) == 1, (name, diagnostic["labels"])
        primary_span = primary[0]["span"]
        assert primary[0]["message"].strip(), (name, primary[0])
        assert primary_span["source"] == str(path), (name, primary_span)
        assert (primary_span["start_byte"], primary_span["end_byte"]) == (
            expected_start,
            expected_end,
        ), (name, primary_span)

        help_annotations = [
            annotation for annotation in diagnostic["annotations"]
            if annotation["kind"] == "help"
        ]
        assert len(help_annotations) == 1, (name, diagnostic["annotations"])
        help_text = help_annotations[0]["message"]
        assert len(help_text.strip()) >= 12, (name, help_text)


def check_valid_contracts_and_returns(directory: Path):
    sources = (
        "proc f(x: int) -> int requires x > 0; ensures result > 0; { return x; }",
        "proc f() requires true; ensures true; { }",
        "proc f(x: bool) -> int { if x { return 1; } else { return 2; } }",
        "proc f() -> int { if true { return 1; } }",
        "proc f() -> int { { return 1; } }",
        "proc f(x: bool) -> int { if x { return 1; } return 2; }",
        "extern proc f() -> int;",
        "proc f() -> int { let result = 1; return result; }",
    )
    for index, source in enumerate(sources):
        path = directory / f"valid-contract-flow-{index}.vit"
        path.write_text(source, encoding="utf-8")
        result = invoke(path, "compile", "json")
        assert result.returncode == 0, (source, result.stderr)
        assert not result.stdout and not result.stderr, (source, result)


def check_invalid_utf8(directory: Path):
    path = directory / "invalid-utf8.vit"
    valid_prefix = b"proc main() -> int { return 0; }\n"
    path.write_bytes(valid_prefix + b"\xff")
    result = invoke(path, "lex", "json")
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    document = json.loads(result.stderr)
    assert len(document["diagnostics"]) == 1, document
    diagnostic = document["diagnostics"][0]
    assert diagnostic["code"] == "E0006", diagnostic
    assert diagnostic["phase"] == "lexer", diagnostic
    assert diagnostic["severity"] == "error", diagnostic
    assert f"byte {len(valid_prefix)}" in diagnostic["details"], diagnostic
    position = diagnostic["position"]
    assert position["source"] == str(path), position
    assert position["has_span"], position
    assert (position["start_byte"], position["end_byte"]) == (
        len(valid_prefix), len(valid_prefix) + 1,
    ), position
    assert (position["start_line"], position["start_column"]) == (1, 0), position
    assert (position["end_line"], position["end_column"]) == (1, 1), position
    assert diagnostic["context"]["context"] == "source encoding", diagnostic
    assert diagnostic["context"]["procedure"] == "source file", diagnostic
    primary = [label for label in diagnostic["labels"] if label["style"] == "primary"]
    assert len(primary) == 1, diagnostic["labels"]
    assert (primary[0]["span"]["start_byte"], primary[0]["span"]["end_byte"]) == (
        len(valid_prefix), len(valid_prefix) + 1,
    ), primary
    helps = [item for item in diagnostic["annotations"] if item["kind"] == "help"]
    assert len(helps) == 1 and len(helps[0]["message"].strip()) >= 12, helps


def check_private_import(directory: Path):
    module_dir = directory / "imports" / "sample"
    module_dir.mkdir(parents=True)
    library = module_dir / "lib.vit"
    library.write_text(
        "space sample::lib;\n"
        "const private_value: i64 = 1;\n"
        "export const public_value: i64 = 2;\n",
        encoding="utf-8",
    )
    path = module_dir.parent / "main.vit"
    source = (
        "space sample::main;\n"
        "use sample::lib;\n"
        "proc main() -> i64 { return lib::private_value; }\n"
    )
    path.write_text(source, encoding="utf-8")
    result = invoke(path, "compile", "json")
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    document = json.loads(result.stderr)
    assert [item["code"] for item in document["diagnostics"]] == ["E0404"], document
    diagnostic = document["diagnostics"][0]
    assert diagnostic["phase"] == "name-resolution", diagnostic
    assert diagnostic["severity"] == "error", diagnostic
    assert diagnostic["internal_code"] and diagnostic["category"], diagnostic
    start = len(source[:source.index("private_value")].encode("utf-8"))
    end = start + len("private_value".encode("utf-8"))
    assert (diagnostic["position"]["start_byte"], diagnostic["position"]["end_byte"]) == (
        start,
        end,
    ), diagnostic
    primary = [label for label in diagnostic["labels"] if label["style"] == "primary"]
    assert len(primary) == 1, diagnostic["labels"]
    assert (primary[0]["span"]["start_byte"], primary[0]["span"]["end_byte"]) == (
        start,
        end,
    ), primary
    assert len(diagnostic["related_locations"]) == 1, diagnostic
    related = diagnostic["related_locations"][0]
    assert related["label"] == "private declaration is here", related
    assert related["span"]["source"] == str(library), related
    declaration = "const private_value: i64 = 1;"
    assert (related["span"]["start_byte"], related["span"]["end_byte"]) == (
        len("space sample::lib;\n".encode("utf-8")),
        len(("space sample::lib;\n" + declaration).encode("utf-8")),
    ), related
    helps = [item for item in diagnostic["annotations"] if item["kind"] == "help"]
    assert len(helps) == 1 and "private_value" in helps[0]["message"], helps


def check_conflicting_import_alias(directory: Path):
    root = directory / "conflicting-imports"
    modules = root / "sample"
    modules.mkdir(parents=True)
    (modules / "lib.vit").write_text(
        "space sample::lib;\nexport const VALUE: i64 = 1;\n",
        encoding="utf-8",
    )
    (modules / "other.vit").write_text(
        "space sample::other;\nexport const VALUE: i64 = 2;\n",
        encoding="utf-8",
    )
    source = (
        "space sample::main;\n"
        "use sample::lib as shared;\n"
        "use sample::other as shared;\n"
        "proc main() -> i64 { return 0; }\n"
    )
    path = root / "main.vit"
    path.write_text(source, encoding="utf-8")
    result = invoke(path, "compile", "json")
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    assert not result.stdout, result.stdout
    document = json.loads(result.stderr)
    assert [item["code"] for item in document["diagnostics"]] == ["E0304"], document
    diagnostic = document["diagnostics"][0]
    assert diagnostic["internal_code"] == "VITTE_IMPORT_E_DUPLICATE", diagnostic
    assert diagnostic["phase"] == "import" and diagnostic["severity"] == "error", diagnostic
    assert diagnostic["message"] == "conflicting import alias", diagnostic
    assert diagnostic["context"]["module"] == "sample::other", diagnostic
    assert diagnostic["context"]["symbol"] == "shared", diagnostic
    marker = "use sample::other as shared;"
    start = len(source[:source.index(marker)].encode("utf-8"))
    end = start + len(marker.encode("utf-8"))
    position = diagnostic["position"]
    assert position["source"] == str(path), position
    assert (position["start_byte"], position["end_byte"]) == (start, end), position
    primary = [label for label in diagnostic["labels"] if label["style"] == "primary"]
    assert len(primary) == 1, diagnostic["labels"]
    assert (primary[0]["span"]["start_byte"], primary[0]["span"]["end_byte"]) == (
        start,
        end,
    ), primary
    helps = [item for item in diagnostic["annotations"] if item["kind"] == "help"]
    assert len(helps) == 1 and "distinct aliases" in helps[0]["message"], helps


def check_import_cycle(directory: Path):
    module_dir = directory / "cycle" / "imports" / "sample"
    module_dir.mkdir(parents=True)
    first = module_dir / "a.vit"
    first_source = "space sample::a;\nuse sample::b;\nconst A: i64 = 1;\n"
    first.write_text(first_source, encoding="utf-8")
    second = module_dir / "b.vit"
    second_source = "space sample::b;\nuse sample::a;\nconst B: i64 = 2;\n"
    second.write_text(second_source, encoding="utf-8")
    path = module_dir.parent / "main.vit"
    path.write_text(
        "space sample::main;\nuse sample::a;\n"
        "proc main() -> i64 { return a::A; }\n",
        encoding="utf-8",
    )
    result = invoke(path, "compile", "json")
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    document = json.loads(result.stderr)
    assert [item["code"] for item in document["diagnostics"]] == ["E0303"], document
    diagnostic = document["diagnostics"][0]
    assert diagnostic["phase"] == "import" and diagnostic["severity"] == "error", diagnostic
    assert diagnostic["context"]["module"] == "sample::b", diagnostic
    assert diagnostic["context"]["procedure"] == "import graph", diagnostic
    assert str(first) in diagnostic["details"] and str(second) in diagnostic["details"], diagnostic
    assert diagnostic["details"].count(" -> ") == 2, diagnostic["details"]
    start = len(first_source[:first_source.index("use sample::b;")].encode("utf-8"))
    end = start + len("use sample::b;".encode("utf-8"))
    assert diagnostic["position"]["source"] == str(first), diagnostic
    assert (diagnostic["position"]["start_byte"], diagnostic["position"]["end_byte"]) == (
        start,
        end,
    ), diagnostic
    primary = [label for label in diagnostic["labels"] if label["style"] == "primary"]
    assert len(primary) == 1, diagnostic["labels"]
    assert (primary[0]["span"]["start_byte"], primary[0]["span"]["end_byte"]) == (
        start,
        end,
    ), primary
    helps = [item for item in diagnostic["annotations"] if item["kind"] == "help"]
    assert len(helps) == 1 and "Break the reported import cycle" in helps[0]["message"], helps


def check_source_code_inventory():
    inline_codes = {code for case in CASES for code in case[3]}
    cli_only_codes = {"E0006", "E0303", "E0304", "E0404"}
    c_api_only_codes = {"E0108"}
    observed = inline_codes | cli_only_codes | c_api_only_codes
    assert observed == ACTIVE_BOOTSTRAP_SOURCE_CODES, {
        "missing_witness": sorted(ACTIVE_BOOTSTRAP_SOURCE_CODES - observed),
        "unclassified_witness": sorted(observed - ACTIVE_BOOTSTRAP_SOURCE_CODES),
    }

    # E0108 is emitted by the public API contract test rather than the CLI
    # case matrix; ensure the exact API witness remains wired into the
    # diagnostic renderer test that compiler-power-gate executes.
    c_test = (root / "src/tests/diagnostic_render_test.c").read_text(encoding="utf-8")
    assert "test_api_invalid_utf8_source_diagnostic" in c_test
    assert 'strstr(diagnostics, "E0108")' in c_test


with tempfile.TemporaryDirectory(prefix="vitte-diagnostic-contract-") as temporary:
    directory = Path(temporary)
    check_source_code_inventory()
    for case in CASES:
        check_case(directory, case)
    check_valid_contracts_and_returns(directory)
    check_invalid_utf8(directory)
    check_private_import(directory)
    check_conflicting_import_alias(directory)
    check_import_cycle(directory)

print(
    "Bootstrap diagnostic contracts passed: "
    f"{len(CASES) + 1} exact source cases, invalid UTF-8, import privacy and cycles."
)
