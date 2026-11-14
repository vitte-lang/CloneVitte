#!/usr/bin/env python3
"""Exercise native parser, semantic, HIR, IR, and backend subset paths."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


VALID_CASES = {
    "generic procedure scalar specialization": """\
proc identity<T>(value: T) -> T { return value; }
proc first<T, U>(left: T, right: U) -> T { return left; }
proc main() -> i64 {
    let value = identity(40);
    return first(value + 2, 7) - 42;
}
""",
    "generic procedure specializes a nested form parameter": """\
form Box<T> { value: T }
proc select_box<T, U>(first: Box<T>, second: Box<U>) -> Box<T> { return first; }
proc main() -> i64 {
    let result: Box<i64> = select_box(
        Box<i64> { value: 42 },
        Box<bool> { value: true }
    );
    return result.value - 42;
}
""",
    "generic form constructor square-bracket type arguments": """\
form Box<T> { value: T }
proc main() -> i64 {
    let boxed: Box[i64] = Box[i64] { value: 42 };
    return boxed.value - 42;
}
""",
    "generic procedure C17 specializations cover bool and multiple parameters": """\
proc identity<T>(value: T) -> T { return value; }
proc first<T, U>(left: T, right: U) -> T { return left; }
proc same<T>(left: T, right: T) -> T { return left; }
#[cold] proc main() -> i64 {
    if not identity(true) { return 1; }
    if identity(40) != 40 { return 2; }
    if first(42, false) != 42 { return 3; }
    return same(42, 40) - 42;
}
""",
    "procedure attribute compatibility fallback": """\
#[cold] proc main() -> i64 { return 0; }
""",
    "native print compatibility fallback": """\
proc main() -> i64 {
    print("native-print-fallback");
    return 0;
}
""",
    "generic procedure falls back when native IR cannot specialize": """\
proc identity<T>(value: T) -> T { return value; }
proc first<T, U>(left: T, right: U) -> T { return left; }
proc same<T>(left: T, right: T) -> T { return left; }
proc is_vitte(text: string) -> bool { return text == "vitte"; }
proc main() -> i64 {
    if not identity(true) { return 1; }
    if identity(40) != 40 { return 2; }
    if identity(1.5) != 1.5 { return 3; }
    if identity('x') != 'x' { return 4; }
    if not is_vitte(identity("vitte")) { return 6; }
    if first(42, false) != 42 { return 7; }
    return same(42, 40) - 42;
}
""",
    "postfix optional unwrap returns its payload": """\
static VALUE: i64 = 42;
proc main() -> i64 {
    let present: ?i64 = &VALUE;
    return present? - 42;
}
""",
    "scalar operators and compound assignment": """\
proc main() -> i64 {
    let mut value: i64 = 1;
    set value += 2 * 3;
    set value -= 1;
    if value == 6 and not false { return 0; }
    return 1;
}
""",
    "mutable field projection": """\
form Point { x: i64 }
proc main() -> i64 {
    let mut point: Point = Point { x: 40 };
    set (point.x) += 2;
    return point.x - 42;
}
""",
    "nested aggregate field mutation through native IR": """\
form Vec2 { x: i64, y: i64 }
form Rect { origin: Vec2, width: i64 }
proc main() -> i64 {
    let mut rect: Rect = Rect {
        origin: Vec2 { y: 20, x: 40 },
        width: 2,
    };
    set (rect.origin.x) += 2;
    return rect.origin.x + rect.origin.y - 62;
}
""",
    "scalar form fields through native IR": """\
form Legacy { value: i32 }
proc main() -> i64 {
    let legacy: Legacy = Legacy { value: 42 };
    return legacy.value - 42;
}
""",
    "fixed-array form fields through native IR": """\
form Packet { values: [3]i64 }
proc main() -> i64 {
    let packet: Packet = Packet { values: [10, 20, 12] };
    return packet.values[2] - 12;
}
""",
    "fixed arrays through native IR": """\
proc main() -> i64 {
    let mut values: [3]i64 = [10, 20, 30];
    set values[1] = 42;
    if values[0] + values[1] + values[2] != 82 { return 1; }
    let mut bytes: [2]u8 = [7 as u8, 255 as u8];
    set bytes[0] = 9 as u8;
    if bytes[0] != 9 or bytes[1] != 255 { return 2; }
    let inferred = [2, 4, 6];
    if inferred[0] + inferred[1] + inferred[2] != 12 { return 3; }
    let mut matrix: [2][2]i64 = [[1, 2], [3, 4]];
    set matrix[1][0] = 7;
    if matrix[1][0] != 7 { return 4; }
    return 0;
}
""",
    "array iteration through native IR": """\
form Packet { values: [3]i64 }
proc sum(values: [2]i64) -> i64 {
    let mut total: i64 = 0;
    for value in values { set total += value; }
    return total;
}
proc main() -> i64 {
    let values: [2]i64 = [20, 22];
    let mut total: i64 = 0;
    for value in values { set total += value; }
    if total != 42 { return 1; }
    let inferred = [2, 4, 6];
    set total = 0;
    for value in inferred {
        if value == 4 { continue; }
        set total += value;
    }
    if total != 8 { return 2; }
    set total = 0;
    for value in [3, 5, 9] {
        if value == 9 { break; }
        set total += value;
    }
    if total != 8 { return 3; }
    let packet: Packet = Packet { values: [1, 2, 3] };
    set total = 0;
    for value in packet.values {
        for other in values { set total += value + other; }
    }
    if total != 138 { return 4; }
    let value: i64 = 99;
    for value in values { if value == 99 { return 5; } }
    if value != 99 { return 6; }
    set total = 0;
    for _ in values { set total += 1; }
    if total != 2 { return 7; }
    let bytes: [2]u8 = [1 as u8, 2 as u8];
    set total = 0;
    for byte in bytes { set total += byte as i64; }
    if total != 3 { return 8; }
    let empty: [0]i64 = [];
    for _ in empty { return 9; }
    for enabled in [true, false] {
        if enabled { continue; }
        set total += 1;
    }
    if total != 4 { return 11; }
    let matrix: [2][2]i64 = [[1, 2], [3, 4]];
    set total = 0;
    for row in matrix {
        for item in row { set total += item; }
    }
    if total != 10 { return 12; }
    if sum(values) != 42 { return 13; }
    let mut changing: [2]i64 = [1, 2];
    set total = 0;
    for item in changing {
        set changing[1] = 12;
        set total += item;
    }
    if total != 13 { return 14; }
    let mut captured: [2]i64 = [1, 2];
    set total = 0;
    for item in captured {
        set captured = [4, 8];
        set total += item;
    }
    if total != 3 { return 15; }
    return 0;
}
""",
    "fixed-array for loop through HIR and IR": """\
proc sum(values: [3]i64) -> i64 {
    let mut total: i64 = 0;
    for value in values { set total += value; }
    return total;
}
proc main() -> i64 { return 0; }
""",
    "scalar picks and match through native IR": """\
pick State { Cold, Ready, Done }
pick Other { Started }

proc state_code(value: State) -> i64 {
    match value {
        State::Cold if value == State::Cold => { return 10; },
        State::Cold => { return 11; },
        State::Ready if value == State::Cold => { return 12; },
        State::Ready => { return 20; },
        else => { return 30; },
    }
    return 0;
}

proc main() -> i64 {
    let mut state: State = State::Cold;
    if state_code(state) != 10 { return 1; }
    set state = State::Ready;
    if state_code(state) != 20 { return 2; }
    set state = State::Done;
    if state_code(state) != 30 { return 3; }
    return 0;
}
""",
    "dotted pick variants through native sema": """\
pick State { Idle, Ready }
pick Maybe { None, Some(i64) }

proc selected_state() -> State {
    return State.Ready;
}

proc selected_value() -> Maybe {
    return Maybe.Some(42);
}

proc main() -> i64 {
    let state: State = selected_state();
    let value: Maybe = selected_value();
    if state == State.Ready { return 0; }
    return 1;
}
""",
    "multi-field payload picks through native IR": """\
pick Payload { Data(i64, bool, i64), Empty }

proc payload_code(value: Payload) -> i64 {
    match value {
        Payload::Data(left, enabled, right) => {
            if enabled { return left + right; }
            return 0;
        },
        Payload::Empty => { return -1; },
    }
    return -2;
}

proc main() -> i64 {
    let value: Payload = Payload::Data(20, true, 22);
    if payload_code(value) != 42 { return 1; }
    let disabled: Payload = Payload::Data(20, false, 22);
    if payload_code(disabled) != 0 { return 2; }
    let empty: Payload = Payload::Empty;
    if payload_code(empty) != -1 { return 3; }
    match value {
        Payload::Data(_, _, _) => { return 0; },
        Payload::Empty => { return 4; },
    }
    return 5;
}
""",
}

INVALID_CASES = {
    "logical operators require booleans": (
        "proc main() -> bool { return 1 and true; }\n",
        "VITTE_TYPE_E_INVALID_OPERAND",
    ),
    "ordering comparisons require numeric operands": (
        'proc main() -> bool { return "left" < "right"; }\n',
        "VITTE_TYPE_E_INVALID_OPERAND",
    ),
    "equality requires compatible operands": (
        'proc main() -> bool { return 1 == "wrong"; }\n',
        "VITTE_TYPE_E_INVALID_OPERAND",
    ),
    "logical negation requires a boolean": (
        "proc main() -> bool { return not 1; }\n",
        "VITTE_TYPE_E_INVALID_OPERAND",
    ),
    "immutable locals cannot use compound assignment": (
        "proc main() -> i64 { let value: i64 = 1; set value += 1; return value; }\n",
        "VITTE_TYPE_E_NOT_ASSIGNABLE",
    ),
    "assignment needs an assignable place": (
        "proc main() -> i64 { set (1 + 2) = 3; return 0; }\n",
        "VITTE_TYPE_E_NOT_ASSIGNABLE",
    ),
    "set requires an assignment expression": (
        "proc main() -> i64 { let mut value: i64 = 1; set value; return value; }\n",
        "VITTE_PARSER_E_UNEXPECTED_TOKEN",
    ),
    "fixed array initializer length must match": (
        "proc main() -> i64 { let values: [2]i64 = [1, 2, 3]; return values[0]; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "form array initializer length must match": (
        "form Packet { values: [3]i64 }\n"
        "proc main() -> i64 { let packet: Packet = Packet { values: [1, 2] }; return 0; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "generic form field type mismatch": (
        'form Box<T> { value: T }\n'
        'proc main() -> i64 { let boxed: Box<i64> = Box<i64> { value: "wrong" }; return 0; }\n',
        "VITTE_TYPE_E_MISMATCH",
    ),
    "generic procedure rejects conflicting nested type arguments": (
        "form Box<T> { value: T }\n"
        "proc same_box<T>(first: Box<T>, second: Box<T>) -> Box<T> { return first; }\n"
        "proc main() -> i64 {\n"
        "    let result: Box<i64> = same_box(\n"
        "        Box<i64> { value: 42 }, Box<bool> { value: true }\n"
        "    );\n"
        "    return result.value;\n"
        "}\n",
        "VITTE_CALL_E_ARGUMENT_TYPE",
    ),
    "generic form first parameter checks field type": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 { let pair: Pair<i64, bool> = Pair<i64, bool> { first: true, second: false }; return 0; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "generic form second parameter checks field type": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 { let pair: Pair<i64, bool> = Pair<i64, bool> { first: 42, second: 7 }; return 0; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "generic form requires every field": (
        "form Box<T> { value: T }\n"
        "proc main() -> i64 { let boxed: Box<i64> = Box<i64> {}; return 0; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "generic form rejects unknown concrete type": (
        "form Box<T> { value: T }\n"
        "proc main() -> i64 { let boxed: Box<Missing> = Box<Missing> { value: 42 }; return 0; }\n",
        "VITTE_TYPE_E_UNKNOWN_TYPE",
    ),
    "nested generic arguments preserve field types": (
        "form Box<T> { value: T }\n"
        "form Outer<T> { wrapped: Box<T> }\n"
        "proc main() -> i64 {\n"
        "    let wrong: Outer<Box<i64>> = Outer<Box<i64>> {\n"
        "        wrapped: Box<Box<bool>> { value: Box<bool> { value: true } },\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "unknown scalar pick variant is rejected": (
        "pick State { Ready, Done }\nproc main() -> i64 { let state: State = State::Missing; return state; }\n",
        "VITTE_TYPE_E_NO_MEMBER",
    ),
    "match pattern must belong to its pick type": (
        "pick State { Ready, Done }\npick Other { Started }\nproc main() -> i64 { let state: State = State::Ready; match state { Other::Started => { return 1; }, else => { return 0; } } return 2; }\n",
        "VITTE_TYPE_E_MISMATCH",
    ),
    "payload pick constructor requires its payload": (
        "pick Maybe { Some(i64) }\nproc main() -> i64 { let value: Maybe = Maybe::Some(); return 0; }\n",
        "VITTE_CALL_E_ARITY",
    ),
    "payload pick constructor checks payload type": (
        'pick Maybe { Some(i64) }\nproc main() -> i64 { let value: Maybe = Maybe::Some("wrong"); return 0; }\n',
        "VITTE_CALL_E_ARGUMENT_TYPE",
    ),
    "payload pick pattern requires its binding": (
        "pick Maybe { Some(i64) }\nproc main() -> i64 { let value: Maybe = Maybe::Some(1); match value { Maybe::Some() => { return 0; } } return 1; }\n",
        "VITTE_CALL_E_ARITY",
    ),
    "multi-field payload pick constructor checks arity": (
        "pick Payload { Data(i64, bool, i64) }\nproc main() -> i64 { let value: Payload = Payload::Data(1, true); return 0; }\n",
        "VITTE_CALL_E_ARITY",
    ),
    "multi-field payload pick checks every field type": (
        'pick Payload { Data(i64, bool, i64) }\nproc main() -> i64 { let value: Payload = Payload::Data(1, true, "wrong"); return 0; }\n',
        "VITTE_CALL_E_ARGUMENT_TYPE",
    ),
    "multi-field payload pick pattern checks arity": (
        "pick Payload { Data(i64, bool, i64) }\nproc main() -> i64 { let value: Payload = Payload::Data(1, true, 2); match value { Payload::Data(first, enabled) => { return first; } } return 0; }\n",
        "VITTE_CALL_E_ARITY",
    ),
    "unknown payload pick variant is rejected": (
        "pick Maybe { Some(i64) }\nproc main() -> i64 { let value: Maybe = Maybe::Missing(1); return 0; }\n",
        "VITTE_TYPE_E_NO_MEMBER",
    ),
}

AST_FALLBACK_CASES = {
    "generic form construction": (
        "form Box<T> { value: T }\n"
        "proc main() -> i64 {\n"
        "    let boxed: Box<i64> = Box<i64> { value: 42 };\n"
        "    return boxed.value - 42;\n"
        "}\n",
        "accept",
    ),
    "constant aliases and else patterns in the C17 fallback": (
        "const FLAG_A: u64 = 1;\n"
        "const FLAG_B: u64 = FLAG_A | 2;\n"
        "form Box<T> { value: T }\n"
        "proc suffix() -> string { return \"x\"; }\n"
        "proc append_suffix(text: string) -> string { return text + suffix(); }\n"
        "proc main() -> i64 {\n"
        "    let boxed: Box<i64> = Box<i64> { value: 42 };\n"
        "    if append_suffix(\"v\") != \"vx\" { return 3; }\n"
        "    match boxed.value {\n"
        "        42 => { return FLAG_B as i64 - 3; },\n"
        "        else => { return 1; },\n"
        "    }\n"
        "    return 2;\n"
        "}\n",
        "accept-runtime",
    ),
    "generic form with multiple parameters compiles and runs": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool> = Pair<i64, bool> { first: 42, second: true };\n"
        "    if pair.first != 42 { return 1; }\n"
        "    if not pair.second { return 2; }\n"
        "    return 0;\n"
        "}\n",
        "accept-runtime",
    ),
    "generic form compositions compile and run": (
        "form Box<T> { value: T }\n"
        "form Wrapper<T> { inner: Box<T> }\n"
        "form Outer<T> { wrapped: Wrapper<T> }\n"
        "form Link<T> { value: T, next: *Link<T> }\n"
        "proc main() -> i64 {\n"
        "    let wrapped: Wrapper<i64> = Wrapper<i64> {\n"
        "        inner: Box<i64> { value: 42 },\n"
        "    };\n"
        "    if wrapped.inner.value != 42 { return 1; }\n"
        "    let enabled: Outer<bool> = Outer<bool> {\n"
        "        wrapped: Wrapper<bool> { inner: Box<bool> { value: true } },\n"
        "    };\n"
        "    if not enabled.wrapped.inner.value { return 2; }\n"
        "    let nested: Outer<Box<i64>> = Outer<Box<i64>> {\n"
        "        wrapped: Wrapper<Box<i64>> {\n"
        "            inner: Box<Box<i64>> { value: Box<i64> { value: 42 } },\n"
        "        },\n"
        "    };\n"
        "    if nested.wrapped.inner.value.value != 42 { return 3; }\n"
        "    let link: Link<i64> = Link<i64> { value: 42, next: null };\n"
        "    if link.value != 42 { return 4; }\n"
        "    return 0;\n"
        "}\n",
        "accept-runtime",
    ),
    "test declaration": (
        'test "native fallback test declaration" { }\n'
        "proc main() -> i64 { return 0; }\n",
        "accept",
    ),
    "unsupported macro declaration": (
        "macro identity() { }\n"
        "proc main() -> i64 { return 0; }\n",
        "unsupported-construct",
    ),
}


def invoke(binary: Path, command: str, source_path: Path):
    return subprocess.run(
        [
            str(binary), command, str(source_path), "--quiet", "--no-color",
            "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )


def diagnostics_for(result: subprocess.CompletedProcess[str], label: str):
    try:
        document = json.loads(result.stderr or result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{label}: expected JSON diagnostics, got stdout={result.stdout!r}, "
            f"stderr={result.stderr!r}"
        ) from error
    return document.get("diagnostics", [])


def assert_ast_fallback(binary: Path, directory: Path, name: str, source: str, expectation: str):
    source_path = directory / (name.replace(" ", "_") + ".vit")
    source_path.write_text(source, encoding="utf-8")
    assert_ast_fallback_path(binary, source_path, name, expectation)


def assert_ast_fallback_path(binary: Path, source_path: Path, name: str, expectation: str):
    result = subprocess.run(
        [
            str(binary), "compile", str(source_path), "--emit-c", "--quiet",
            "--no-color", "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if expectation in ("accept", "accept-runtime"):
        if result.returncode != 0:
            raise AssertionError(
                f"{name}: expected compatibility emitter success, got {result.returncode}; "
                f"stdout={result.stdout!r}; stderr={result.stderr!r}"
            )
        if "Generated by the native Vitte C17 AST emitter" not in result.stdout:
            raise AssertionError(f"{name}: compatibility emitter did not produce C output")
        if expectation == "accept-runtime":
            executed = invoke(binary, "run", source_path)
            if executed.returncode != 0:
                raise AssertionError(
                    f"{name}: generated program should compile and run; "
                    f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                )
        return

    if result.returncode != 1:
        raise AssertionError(
            f"{name}: expected a checked fallback rejection, got {result.returncode}; "
            f"stdout={result.stdout!r}; stderr={result.stderr!r}"
        )
    diagnostics = diagnostics_for(result, name)
    codegen_diagnostic = next(
        (
            item
            for item in diagnostics
            if item.get("internal_code") == "VITTE_BACKEND_E_CODEGEN"
        ),
        None,
    )
    if codegen_diagnostic is None:
        raise AssertionError(f"{name}: expected owned backend codegen diagnostic, got {diagnostics!r}")
    if expectation == "unsupported-construct":
        position = codegen_diagnostic.get("position", {})
        if (
            "macro declaration" not in codegen_diagnostic.get("message", "")
            or "expand or remove" not in codegen_diagnostic.get("details", "")
            or not position.get("has_span")
            or position.get("start_line") != 0
            or position.get("start_column") != 0
        ):
            raise AssertionError(
                f"{name}: expected a construct-specific diagnostic at the macro span, "
                f"got {codegen_diagnostic!r}"
            )
def assert_generic_form_runtime(binary: Path):
    source_path = (
        Path(__file__).resolve().parents[2]
        / "examples"
        / "tutorials"
        / "formes-generiques.vit"
    )
    if not source_path.is_file():
        raise AssertionError(f"generic form example is missing: {source_path}")

    checked = invoke(binary, "check", source_path)
    if checked.returncode != 0:
        raise AssertionError(
            f"generic form example: check returned {checked.returncode}; "
            f"stdout={checked.stdout!r}; stderr={checked.stderr!r}"
        )

    emitted = subprocess.run(
        [
            str(binary), "compile", str(source_path), "--emit-c", "--quiet",
            "--no-color", "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if emitted.returncode != 0:
        raise AssertionError(
            f"generic form example: C generation returned {emitted.returncode}; "
            f"stdout={emitted.stdout!r}; stderr={emitted.stderr!r}"
        )
    if "Generated by the native Vitte C17 AST emitter" not in emitted.stdout:
        raise AssertionError("generic form example: expected the C17 AST compatibility emitter")

    executed = invoke(binary, "run", source_path)
    if executed.returncode != 0:
        raise AssertionError(
            f"generic form example: run returned {executed.returncode}; "
            f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} VITTE", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="vitte-native-frontend-") as value:
        directory = Path(value)
        c17_fallback_cases = {
            "generic procedure C17 specializations cover bool and multiple parameters",
            "generic procedure falls back when native IR cannot specialize",
            "postfix optional unwrap returns its payload",
            "procedure attribute compatibility fallback",
            "native print compatibility fallback",
        }
        for name, source in VALID_CASES.items():
            source_path = directory / (name.replace(" ", "_") + ".vit")
            source_path.write_text(source, encoding="utf-8")
            checked = invoke(binary, "check", source_path)
            if checked.returncode != 0:
                raise AssertionError(
                    f"{name}: check returned {checked.returncode}; "
                    f"stdout={checked.stdout!r}; stderr={checked.stderr!r}"
                )
            if name in c17_fallback_cases:
                assert_ast_fallback_path(binary, source_path, name, "accept")
            if name == "scalar operators and compound assignment":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "generic form constructor square-bracket type arguments":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "generic procedure scalar specialization":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: generic procedure runtime failed; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "generic procedure specializes a nested form parameter":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"nested generic procedure runtime failed; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "generic procedure C17 specializations cover bool and multiple parameters":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: specialized C17 runtime failed; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "generic procedure falls back when native IR cannot specialize":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: native fallback runtime failed; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "native print compatibility fallback":
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0 or "native-print-fallback" not in executed.stdout:
                    raise AssertionError(
                        f"{name}: expected printed fallback output; "
                        f"return={executed.returncode}; stdout={executed.stdout!r}; "
                        f"stderr={executed.stderr!r}"
                    )
            if name == "postfix optional unwrap returns its payload":
                output_path = directory / "optional_unwrap.c"
                emitted = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-c",
                        "--quiet", "--no-color", "-o", str(output_path),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                generated = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
                if emitted.returncode != 0 or "vitte_internal_optional_require_" not in generated:
                    raise AssertionError(
                        "C17 must emit a checked optional unwrap temporary; "
                        f"return={emitted.returncode}; stderr={emitted.stderr!r}; "
                        f"generated={generated[-1000:]!r}"
                    )
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: present optional should unwrap successfully; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "array iteration through native IR":
                lowered = subprocess.run(
                    [str(binary), "compile", str(source_path), "--emit-ir", "--no-color"],
                    capture_output=True, text=True, timeout=60,
                )
                for opcode in ("gep", "icmp_ult", "cond_br"):
                    if lowered.returncode != 0 or opcode not in lowered.stdout:
                        raise AssertionError(f"{name}: missing {opcode}; {lowered.stdout!r}; {lowered.stderr!r}")
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(f"{name}: run returned {executed.returncode}; {executed.stderr!r}")
            if name == "fixed-array for loop through HIR and IR":
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                for opcode in ("icmp_ult", "gep", "cond_br"):
                    if lowered.returncode != 0 or opcode not in lowered.stdout:
                        raise AssertionError(
                            f"{name}: expected {opcode} in lowered IR; "
                            f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                            f"stderr={lowered.stderr!r}"
                        )
            if name == "fixed arrays through native IR":
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if lowered.returncode != 0 or "array_make" not in lowered.stdout or "gep" not in lowered.stdout:
                    raise AssertionError(
                        f"fixed arrays: expected array construction and indexed IR; "
                        f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                        f"stderr={lowered.stderr!r}"
                    )
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"fixed arrays: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name in ("mutable field projection", "nested aggregate field mutation through native IR"):
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                for opcode in ("struct_make", "struct_field", "load"):
                    if lowered.returncode != 0 or opcode not in lowered.stdout:
                        raise AssertionError(
                            f"{name}: expected {opcode} in native aggregate IR; "
                            f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                            f"stderr={lowered.stderr!r}"
                        )
                if name == "nested aggregate field mutation through native IR" and "store" not in lowered.stdout:
                    raise AssertionError("nested aggregate mutation: expected store in native IR")
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name in (
                "scalar form fields through native IR",
                "fixed-array form fields through native IR",
            ):
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                expected_opcodes = ["struct_make", "struct_field"]
                if name == "fixed-array form fields through native IR":
                    expected_opcodes.extend(("array_make", "gep"))
                for opcode in expected_opcodes:
                    if lowered.returncode != 0 or opcode not in lowered.stdout:
                        raise AssertionError(
                            f"{name}: expected {opcode} in native IR; "
                            f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                            f"stderr={lowered.stderr!r}"
                        )

                output_path = directory / (name.replace(" ", "_") + ".c")
                emitted = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-c",
                        "--quiet", "--no-color", "-o", str(output_path),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if emitted.returncode != 0:
                    raise AssertionError(
                        f"{name}: code generation returned {emitted.returncode}; "
                        f"stdout={emitted.stdout!r}; stderr={emitted.stderr!r}"
                    )
                generated = output_path.read_text(encoding="utf-8")
                if "Generated by the native Vitte C17 backend" not in generated:
                    raise AssertionError(
                        f"{name}: expected the HIR/IR C17 backend, got compatibility AST output"
                    )

                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"{name}: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "scalar picks and match through native IR":
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if lowered.returncode != 0 or "icmp_eq" not in lowered.stdout or "cond_br" not in lowered.stdout:
                    raise AssertionError(
                        f"scalar picks: expected equality and branch IR; "
                        f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                        f"stderr={lowered.stderr!r}"
                    )
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"scalar picks: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )
            if name == "multi-field payload picks through native IR":
                lowered = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-ir",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                for opcode in ("pick_make", "pick_tag", "pick_payload", "cond_br"):
                    if lowered.returncode != 0 or opcode not in lowered.stdout:
                        raise AssertionError(
                            f"multi-field payload picks: expected {opcode} in native IR; "
                            f"return={lowered.returncode}; stdout={lowered.stdout!r}; "
                            f"stderr={lowered.stderr!r}"
                        )
                executed = invoke(binary, "run", source_path)
                if executed.returncode != 0:
                    raise AssertionError(
                        f"multi-field payload picks: run returned {executed.returncode}; "
                        f"stdout={executed.stdout!r}; stderr={executed.stderr!r}"
                    )

        for name, (source, expected_code) in INVALID_CASES.items():
            source_path = directory / (name.replace(" ", "_") + ".vit")
            source_path.write_text(source, encoding="utf-8")
            checked = invoke(binary, "check", source_path)
            if checked.returncode != 1:
                raise AssertionError(
                    f"{name}: expected rejection, got {checked.returncode}; "
                    f"stdout={checked.stdout!r}; stderr={checked.stderr!r}"
                )
            diagnostics = diagnostics_for(checked, name)
            codes = {item.get("internal_code") for item in diagnostics}
            if expected_code not in codes:
                raise AssertionError(
                    f"{name}: expected {expected_code}, got {diagnostics!r}"
                )
            if name.startswith("generic form "):
                compiled = subprocess.run(
                    [
                        str(binary), "compile", str(source_path), "--emit-c",
                        "--quiet", "--no-color", "--diagnostic=json",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if compiled.returncode != 1:
                    raise AssertionError(
                        f"{name}: compile should reject invalid generic form, got "
                        f"{compiled.returncode}; stdout={compiled.stdout!r}; "
                        f"stderr={compiled.stderr!r}"
                    )
                compile_diagnostics = diagnostics_for(compiled, name)
                compile_codes = {item.get("internal_code") for item in compile_diagnostics}
                if expected_code not in compile_codes:
                    raise AssertionError(
                        f"{name}: compile expected {expected_code}, got "
                        f"{compile_diagnostics!r}"
                    )

        missing_optional = directory / "null_optional_unwrap.vit"
        missing_optional.write_text(
            "proc main() -> i64 { let missing: ?i64 = null; return missing?; }\n",
            encoding="utf-8",
        )
        null_run = invoke(binary, "run", missing_optional)
        null_diagnostics = diagnostics_for(null_run, "null optional unwrap")
        if null_run.returncode != 1 or not any(
            item.get("phase") == "runtime"
            and item.get("internal_code")
            in {"VITTE_BACKEND_E_EXECUTION", "VITTE_BACKEND_E_RUNTIME"}
            for item in null_diagnostics
        ):
            raise AssertionError(
                "unwrapping null must compile and fail at runtime; "
                f"return={null_run.returncode}; diagnostics={null_diagnostics!r}; "
                f"stdout={null_run.stdout!r}; "
                f"stderr={null_run.stderr!r}"
            )

        for name, (source, expectation) in AST_FALLBACK_CASES.items():
            assert_ast_fallback(binary, directory, name, source, expectation)

        runtime_fallbacks = (
            ("module alias import compatibility fallback", "llvm_imports_runtime.vit"),
            ("string intrinsic compatibility fallback", "string_intrinsics_runtime.vit"),
            ("generic pick compatibility fallback", "generic_pick_runtime.vit"),
        )
        for name, fixture in runtime_fallbacks:
            source_path = Path(__file__).with_name(fixture).resolve()
            assert_ast_fallback_path(binary, source_path, name, "accept-runtime")

    assert_generic_form_runtime(binary)

    print(
        f"Native frontend subset passed: {len(VALID_CASES)} valid and "
        f"{len(INVALID_CASES)} invalid cases; generic form runtime passed."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
