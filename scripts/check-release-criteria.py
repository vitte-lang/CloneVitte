#!/usr/bin/env python3
"""Block release gates on diagnostic ownership or untested frontend paths."""

from __future__ import annotations

import re
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
NATIVE_REGISTRY = ROOT / "src/compiler/diagnostic/registry.vitte"
C_REGISTRY = ROOT / "src/bootstrap/diagnostic/registry.c"
PIPELINE = ROOT / "src/compiler/driver/pipeline.vitte"
FRONTEND_TEST = ROOT / "src/tests/native_frontend_subset_test.py"
PARSER_TEST = ROOT / "src/tests/native_parser_parity_test.c"
BOOTSTRAP_PARITY_TEST = ROOT / "src/tests/native_bootstrap_parity_test.py"
ROBUSTNESS_TEST = ROOT / "src/tests/robustness_test.c"


def fail(message: str) -> None:
    raise SystemExit(f"[compiler-release-criteria][error] {message}")


def split_macro_arguments(text: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    quote: str | None = None
    escaped = False
    for index, char in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    result.append(text[start:].strip())
    return result


def parse_native_registry() -> dict[str, dict[str, str]]:
    text = NATIVE_REGISTRY.read_text(encoding="utf-8")
    row_pattern = re.compile(r"DiagnosticRegistryEntry\s*\{([^{}]*)\}")
    field_pattern = re.compile(r'\b(\w+)\s*:\s*"((?:\\.|[^"\\])*)"')
    rows: dict[str, dict[str, str]] = {}
    public_codes: dict[str, str] = {}
    required = ("internal_code", "public_code", "category", "title", "explanation")

    for match in row_pattern.finditer(text):
        fields = dict(field_pattern.findall(match.group(1)))
        if "internal_code" not in fields:
            continue  # The exported record declaration is not a registry entry.
        for name in required:
            if not fields.get(name, "").strip():
                fail(f"native diagnostic {fields.get('internal_code', '<unknown>')} has empty {name}")
        if not re.fullmatch(r"[EWHN]\d{4}", fields["public_code"]):
            fail(f"invalid public diagnostic code: {fields['public_code']}")
        severity = re.search(r"default_severity:\s*DiagnosticSeverity\.(\w+)", match.group(1))
        origin = re.search(r"origin:\s*DiagnosticOrigin\.(\w+)", match.group(1))
        if not severity or severity.group(1) not in {"Error", "Fatal", "Warning"}:
            fail(f"diagnostic {fields['internal_code']} has no recognized default severity")
        if not origin or origin.group(1) in {"Unknown", "Invalid"}:
            fail(f"diagnostic {fields['internal_code']} has no owned origin")
        fields["default_severity"] = severity.group(1)
        fields["origin"] = origin.group(1)
        internal = fields["internal_code"]
        public = fields["public_code"]
        if internal in rows:
            fail(f"duplicate internal diagnostic code: {internal}")
        if public in public_codes:
            fail(f"public diagnostic {public} is shared by {public_codes[public]} and {internal}")
        rows[internal] = fields
        public_codes[public] = internal

    if not rows:
        fail("native diagnostic registry contains no entries")
    return rows


def parse_c_registry() -> dict[str, dict[str, str]]:
    text = C_REGISTRY.read_text(encoding="utf-8")
    marker = "vitte_registry_entries[] = {"
    if marker not in text:
        fail("C diagnostic registry table was not found")
    text = text.split(marker, 1)[1]
    calls = re.finditer(r"\b(ENTRY|WARNING_ENTRY)\s*\(", text)
    rows: dict[str, dict[str, str]] = {}
    public_codes: dict[str, str] = {}

    for match in calls:
        depth = 1
        index = match.end()
        quote: str | None = None
        escaped = False
        while index < len(text) and depth:
            char = text[index]
            if quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    quote = None
            elif char in "\"'":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1
        if depth:
            fail("unterminated entry in C diagnostic registry")
        args = split_macro_arguments(text[match.end():index - 1])
        expected_count = 6 if match.group(1) == "WARNING_ENTRY" else 7
        if len(args) != expected_count:
            fail(f"C diagnostic entry has {len(args)} fields, expected {expected_count}")
        internal = args[0].strip(' "\t\r\n')
        public = args[1].strip(' "\t\r\n')
        if not internal or not public:
            fail("C diagnostic entry has an empty internal or public code")
        if not re.fullmatch(r"[EWHN]\d{4}", public):
            fail(f"C diagnostic {internal} has invalid public code {public}")
        if internal in rows:
            fail(f"duplicate C internal diagnostic code: {internal}")
        if public in public_codes:
            fail(f"C public diagnostic {public} is shared by {public_codes[public]} and {internal}")
        if match.group(1) == "WARNING_ENTRY":
            severity = "Warning"
            origin_index, category_index = 2, 3
        else:
            severity_name = args[2].strip()
            severity = {
                "VITTE_DIAGNOSTIC_ERROR": "Error",
                "VITTE_DIAGNOSTIC_FATAL": "Fatal",
                "VITTE_DIAGNOSTIC_WARNING": "Warning",
            }.get(severity_name, "")
            origin_index, category_index = 3, 4
        origin_name = args[origin_index].strip()
        origin_suffix = origin_name.removeprefix("VITTE_DIAGNOSTIC_ORIGIN_")
        origin = {
            "C17_BACKEND": "C17Backend",
            "CONSTANT_EVAL": "ConstantEval",
            "NAME_RESOLUTION": "NameResolution",
            "TYPE_CHECK": "TypeCheck",
        }.get(origin_suffix, "".join(part.title() for part in origin_suffix.split("_")))
        text_fields: list[str] = []
        for arg in args[category_index:category_index + 3]:
            try:
                value = json.loads(arg)
            except json.JSONDecodeError:
                value = ""
            text_fields.append(value if isinstance(value, str) else "")
        if not severity or not origin or any(not value.strip() for value in text_fields):
            fail(f"C diagnostic {internal} has incomplete severity/origin/metadata")
        rows[internal] = {
            "public_code": public,
            "default_severity": severity,
            "origin": origin,
            "category": text_fields[0],
            "title": text_fields[1],
            "explanation": text_fields[2],
        }
        public_codes[public] = internal

    if not rows:
        fail("C diagnostic registry contains no entries")
    return rows


def strip_comments(text: str, *, c_source: bool) -> str:
    if c_source:
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def check_diagnostic_ownership(
    native: dict[str, dict[str, str]], c_rows: dict[str, dict[str, str]]
) -> None:
    native_codes = {key: value["public_code"] for key, value in native.items()}
    missing_native = sorted(set(c_rows) - set(native_codes))
    missing_c = sorted(set(native_codes) - set(c_rows))
    if missing_native or missing_c:
        fail(f"C/native diagnostic registries differ: C-only={missing_native}; native-only={missing_c}")
    changed = sorted(
        (internal, c_rows[internal]["public_code"], native_codes[internal])
        for internal in native_codes
        if c_rows[internal]["public_code"] != native_codes[internal]
    )
    if changed:
        fail(f"C/native public diagnostic mapping differs: {changed}")

    metadata_fields = ("default_severity", "origin", "category", "title", "explanation")
    metadata_differences = []
    for internal in native:
        for field in metadata_fields:
            expected = native[internal].get(field, "")
            if c_rows[internal].get(field) != expected:
                metadata_differences.append((internal, field, c_rows[internal].get(field), expected))
    if metadata_differences:
        fail(f"C/native diagnostic metadata differs: {metadata_differences[:12]}")

    known = set(native)
    sources = [*ROOT.glob("src/bootstrap/**/*.c"), *ROOT.glob("src/bootstrap/**/*.h"), *ROOT.glob("src/compiler/**/*.vitte")]
    unowned: dict[str, str] = {}
    symbol_pattern = re.compile(r"\bVITTE_[A-Z0-9_]*(?:_E_|_W_|_N_|_H_)[A-Z0-9_]+\b")
    for path in sources:
        if path.name == "diagnostic.h":
            continue  # It declares legacy API status names as well as diagnostics.
        source = strip_comments(path.read_text(encoding="utf-8", errors="replace"), c_source=path.suffix in {".c", ".h"})
        for symbol in symbol_pattern.findall(source):
            if symbol not in known:
                unowned.setdefault(symbol, path.relative_to(ROOT).as_posix())
    if unowned:
        fail("unowned diagnostic symbols: " + ", ".join(f"{code} ({path})" for code, path in sorted(unowned.items())))


def check_frontend_paths() -> None:
    pipeline = PIPELINE.read_text(encoding="utf-8")
    frontend_test = FRONTEND_TEST.read_text(encoding="utf-8")
    parser_test = PARSER_TEST.read_text(encoding="utf-8")
    parity_test = BOOTSTRAP_PARITY_TEST.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")

    action_witnesses = {
        "NATIVE_FRONTEND_ACTION_COMPILE": '"compile", str(source_path)',
        "NATIVE_FRONTEND_ACTION_CHECK": 'invoke(binary, "check", source_path)',
        "NATIVE_FRONTEND_ACTION_EMIT_IR": '"--emit-ir"',
        "NATIVE_FRONTEND_ACTION_EMIT_AST_FINGERPRINT": "VITTE_NATIVE_FRONTEND_ACTION_EMIT_AST_FINGERPRINT",
    }
    for action, witness in action_witnesses.items():
        if action not in pipeline:
            fail(f"frontend action is missing from pipeline: {action}")
        witness_source = parser_test if action.endswith("EMIT_AST_FINGERPRINT") else frontend_test
        if witness not in witness_source:
            fail(f"frontend action has no test witness: {action} ({witness})")

    fallback_witnesses = {
        "FormDecl": ("generic form compositions compile and run", "form Wrapper<T>"),
        "TestDecl": ("test declaration", 'test "native fallback test declaration"'),
    }
    for witness in ("array iteration through native IR", "for value in values"):
        if witness not in frontend_test:
            fail(f"native array iteration has no checked fixture: {witness}")
    fallback_function = pipeline.split("proc native_pipeline_requires_ast_codegen", 1)
    if len(fallback_function) != 2:
        fail("native AST compatibility fallback selector is missing")
    fallback_body = fallback_function[1].split("\nproc ", 1)[0]
    for kind, (case_name, source_marker) in fallback_witnesses.items():
        if f"ASTNodeKind.{kind}" not in fallback_body:
            fail(f"AST compatibility fallback path is missing: {kind}")
        if case_name not in frontend_test or source_marker not in frontend_test:
            fail(f"AST compatibility fallback path has no checked fixture: {kind} ({case_name})")

    selector_witnesses = {
        "native_pipeline_has_module_alias_import": (
            "module alias import compatibility fallback",
            "llvm_imports_runtime.vit",
            'use tests::import_export_coverage::runtime_import_lib as imported;',
        ),
        "native_pipeline_has_string_intrinsic_call": (
            "string intrinsic compatibility fallback",
            "string_intrinsics_runtime.vit",
            'trim("  fn(int) -> bool  ")',
        ),
        "native_pipeline_has_print_call": (
            "native print compatibility fallback",
            'print("native-print-fallback")',
            'print("native-print-fallback")',
        ),
        "native_pipeline_requires_generic_pick_ast_codegen": (
            "generic pick compatibility fallback",
            "generic_pick_runtime.vit",
            "pick Maybe<T>",
        ),
        "native_pipeline_requires_reference_codegen": (
            "postfix optional unwrap returns its payload",
            "vitte_optional_require",
            "present? - 42",
        ),
        "native_pipeline_requires_attribute_codegen": (
            "procedure attribute compatibility fallback",
            "procedure attribute compatibility fallback",
            "#[cold] proc main()",
        ),
        "native_pipeline_has_generic_proc_call": (
            "generic procedure falls back when native IR cannot specialize",
            "generic procedure falls back when native IR cannot specialize",
            "proc identity<T>(value: T) -> T",
        ),
    }
    run_function = pipeline.split("proc native_pipeline_run_with_config", 1)
    if len(run_function) != 2:
        fail("native frontend pipeline entry point is missing")
    run_body = run_function[1].split("\nproc ", 1)[0]
    for selector, (case_name, fixture_or_marker, source_marker) in selector_witnesses.items():
        if selector not in run_body:
            fail(f"native frontend pipeline no longer routes through {selector}")
        if case_name not in frontend_test:
            fail(f"native fallback selector has no checked test case: {selector} ({case_name})")
        if fixture_or_marker.endswith(".vit"):
            fixture_path = ROOT / "src/tests" / fixture_or_marker
            if not fixture_path.is_file():
                fail(f"native fallback runtime fixture is missing: {fixture_or_marker}")
            if fixture_or_marker not in frontend_test:
                fail(f"native fallback fixture is not invoked by the frontend test: {fixture_or_marker}")
            runtime_candidates = makefile.split("RUNTIME_CANDIDATES :=", 1)[1].split(
                "RUNTIME_KNOWN_GAPS :=", 1
            )[0]
            if fixture_or_marker not in runtime_candidates:
                fail(f"native fallback fixture is not run by test-runtime: {fixture_or_marker}")
            fixture_text = fixture_path.read_text(encoding="utf-8")
            if source_marker not in fixture_text:
                fail(f"native fallback fixture lost its source witness: {fixture_or_marker}")
        elif source_marker not in frontend_test:
            fail(f"native fallback test case lost its source witness: {selector} ({source_marker})")

    # The broad AST selector must retain a checked witness for each compatibility
    # branch, including branches that reject unsupported forms at codegen time.
    ast_markers = (
        "node.attribute_count != 0",
        "PARSER_AST_FLAG_COMPTIME",
        '(*declaration_token).lexeme == "intrinsic"',
        "ASTNodeKind.AwaitExpr",
        "PARSER_AST_FLAG_ASYNC",
        "ASTNodeKind.AssertStmt",
        "ASTNodeKind.DeferStmt",
        "ASTNodeKind.UnsafeBlock",
        "ASTNodeKind.AsmStmt",
    )
    for marker in ast_markers:
        if marker not in fallback_body:
            fail(f"AST compatibility selector lost a fallback branch: {marker}")
    if "comptime" not in parity_test or "unknown_attribute" not in parity_test:
        fail("comptime/attribute compatibility rejections have no parity test witness")
    runtime_section = makefile.split("RUNTIME_CANDIDATES :=", 1)[1].split(
        "RUNTIME_KNOWN_GAPS :=", 1
    )[0]
    known_gaps = makefile.split("RUNTIME_KNOWN_GAPS :=", 1)[1].split(
        "RUNTIME_EXECUTABLE_CANDIDATES :=", 1
    )[0]
    ast_runtime_witnesses = {
        "async_runtime.vit": ("async proc", "await "),
        "assert_runtime.vit": ("assert(", "unsafe {"),
        "asm_runtime.vit": ("asm ",),
        "defer_runtime.vit": ("defer",),
        "intrinsic_runtime.vit": ("intrinsic ",),
    }
    for fixture, markers in ast_runtime_witnesses.items():
        fixture_path = ROOT / "src/tests" / fixture
        if not fixture_path.is_file() or fixture not in runtime_section:
            fail(f"AST compatibility runtime branch is not tested: {fixture}")
        if fixture in known_gaps:
            fail(f"AST compatibility runtime witness is marked as a known gap: {fixture}")
        fixture_text = fixture_path.read_text(encoding="utf-8")
        if any(marker not in fixture_text for marker in markers):
            fail(f"AST compatibility runtime fixture lost its branch witness: {fixture}")
    if "test-runtime" not in makefile.split("compiler-power-gate:", 1)[1].split(
        "# Maximum fast local gate", 1
    )[0]:
        fail("compiler-power-gate does not execute native runtime fallback witnesses")


def check_filesystem_ownership_witnesses() -> None:
    robustness_test = ROBUSTNESS_TEST.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    active_filesystem_imports = (
        "vitte_filesystem_create_directories",
        "vitte_filesystem_current_directory",
        "vitte_filesystem_directory_list_destroy",
        "vitte_filesystem_list_directory",
        "vitte_filesystem_remove_directory",
        "vitte_filesystem_remove_file",
        "vitte_filesystem_rename",
        "vitte_filesystem_stat",
    )
    for symbol in active_filesystem_imports:
        if symbol not in robustness_test:
            fail(f"active filesystem C ABI has no robustness-test witness: {symbol}")
    test_target = makefile.split(".PHONY: test-robustness", 1)[1].split("\n", 2)[1]
    if "ROBUSTNESS_TEST_BIN" not in test_target:
        fail("filesystem ABI robustness test is not connected to test-robustness")
    power_gate = makefile.split("compiler-power-gate:", 1)[1].split(
        "# Maximum fast local gate", 1
    )[0]
    if "test-robustness" not in power_gate:
        fail("compiler-power-gate does not execute filesystem ABI witnesses")

def main() -> int:
    native = parse_native_registry()
    c_rows = parse_c_registry()
    check_diagnostic_ownership(native, c_rows)
    check_frontend_paths()
    check_filesystem_ownership_witnesses()
    print(
        "Release criteria passed: "
        f"{len(native)} owned diagnostics, matched C/native codes, "
        "all frontend actions and fallback routes have test witnesses, "
        "and every active bootstrap filesystem ABI import is covered."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        print(f"[compiler-release-criteria][error] {error}", file=sys.stderr)
        raise SystemExit(1)
