"""Exercise contextual help through the real compiler and structured formats."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
diagnostic_binary = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else binary
root = Path(__file__).resolve().parents[2]
version = (root / "VERSION").read_text().strip()
assert subprocess.check_output([binary, "--version"], text=True).splitlines()[0] == f"vitte {version}"

explanation = json.loads(subprocess.check_output(
    [binary, "explain", "E1503", "--diagnostic=json"], text=True))
assert "zero divisor" in explanation["diagnostic"]["guidance"]
assert "zero divisor" in subprocess.check_output([binary, "explain", "E1503"], text=True)
invalid = subprocess.run([binary, "explain", "E9999", "--diagnostic=json"],
                         capture_output=True, text=True)
assert invalid.returncode == 2
assert "unknown diagnostic code" in invalid.stderr


def run(path, fmt="json", command="compile"):
    args = [str(diagnostic_binary), command, str(path), "--quiet", "--no-color", f"--diagnostic={fmt}"]
    if command == "compile":
        args.append("--stop-after-sema")
    result = subprocess.run(args, capture_output=True, text=True)
    assert result.returncode == 1, (args, result.returncode, result.stdout, result.stderr)
    if fmt == "terminal":
        return result.stderr
    return json.loads(result.stderr)


def help_for(document, code):
    items = [d for d in document["diagnostics"] if d["code"] == code]
    assert items, document
    assert items[0]["severity"] == "error", items[0]
    expected_phase = {
        "E0005": "io", "E0102": "lexer", "E0201": "parser",
        "E0401": "name-resolution", "E0601": "type-check",
        "E0602": "type-check", "E0605": "type-check",
    }[code]
    assert items[0]["phase"] == expected_phase, items[0]
    helps = [a["message"] for a in items[0]["annotations"] if a["kind"] == "help"]
    assert len(helps) == 1, items[0]
    return helps[0]


with tempfile.TemporaryDirectory(prefix="vitte-help-") as directory:
    path = Path(directory) / "case.vit"

    def case(source, code, expected, absent=None):
        path.write_text(source)
        help_text = help_for(run(path), code)
        assert expected in help_text, help_text
        if absent:
            assert absent not in help_text, help_text
        assert help_text in run(path, "terminal"), help_text
        sarif = run(path, "sarif")
        assert sarif["runs"][0]["tool"]["driver"]["version"] == version
        result = next(r for r in sarif["runs"][0]["results"] if r["ruleId"] == code)
        assert result["properties"]["help"] == help_text, result
        assert result["level"] == "error", result
        assert result["properties"]["severity"] == "error", result
        assert result["properties"]["phase"] == (
            "name-resolution" if code == "E0401" else "type-check"), result
        if code == "E0602":
            document = run(path)
            diagnostic = next(d for d in document["diagnostics"] if d["code"] == code)
            assert diagnostic["context"]["expected_type"] == "i64", diagnostic
            assert diagnostic["context"]["actual_type"] == "string", diagnostic
            assert result["properties"]["expectedType"] == "i64", result
            assert result["properties"]["actualType"] == "string", result
            terminal = run(path, "terminal")
            assert "expected i64, found string" in terminal, terminal
        return help_text

    case("proc main() -> int { let user_count = 2; return user_coutn; }",
         "E0401", "Did you mean `user_count` instead of `user_coutn`?")
    # Deletion, insertion, substitution, and transposition all stay advisory.
    for typo in ("user_coun", "user_countt", "user_coumt"):
        case(f"proc main() -> int {{ let user_count = 2; return {typo}; }}",
             "E0401", "Did you mean `user_count`")
    case("proc main() -> int { let count = 2; let court = 3; return cout; }",
         "E0401", "`cout`", "Did you mean")
    case("proc helper() -> int { let user_count = 2; return user_count; }\n"
         "proc main() -> int { return user_coutn; }",
         "E0401", "`user_coutn`", "Did you mean")
    case("proc main() -> int { let ab = 2; return ac; }",
         "E0401", "`ac`", "Did you mean")
    case("proc add(a: int, b: int) -> int { return a + b; }\n"
         "proc main() -> int { return add(1); }",
         "E0601", "requires 2 argument(s), but 1 were supplied. Add 1")
    case("proc add(a: int, b: int) -> int { return a + b; }\n"
         "proc main() -> int { return add(1, 2, 3); }",
         "E0601", "requires 2 argument(s), but 3 were supplied. Remove 1")
    case("proc main() -> int { return len(); }",
         "E0601", "requires 1 argument(s), but 0 were supplied. Add 1")
    case('proc add(a: int) -> int { return a; }\nproc main() -> int { return add("42"); }',
         "E0602", "Argument 1 does not match parameter `a` of procedure `add`.")
    case('proc main() -> int { return "nope"; }',
         "E0605", "Return value in procedure `main` does not match its declared return type.")
    path.write_text("proc main() -> int { return (1 + 2; }")
    syntax = help_for(run(path, command="check"), "E0201")
    assert "`)`" in syntax and "`;`" in syntax, syntax
    path.write_text('proc main() -> int { let value = "unterminated;\n }')
    lexical = help_for(run(path, command="check"), "E0102")
    assert "matching quote" in lexical, lexical
    for source, replacement in (
        ("proc main() -> bool { return true && false; }", "and"),
        ("proc main() -> bool { return true || false; }", "or"),
        ("proc main() -> bool { return !true; }", "not"),
    ):
        path.write_text(source)
        document = run(path, command="check")
        diagnostic = document["diagnostics"][0]
        assert diagnostic["suggestions"], diagnostic
        assert diagnostic["suggestions"][0]["replacement"] == replacement, diagnostic
        assert replacement in run(path, "terminal", command="check"), diagnostic
    path.write_text("proc main() -> int { let value = (1; return value; }")
    punctuation = run(path, command="check")["diagnostics"][0]
    assert punctuation["suggestions"][0]["replacement"] == ")", punctuation
    path.write_text('proc main() -> int { let value = "bad\\q"; }')
    invalid_escape_json = run(path, command="check")
    invalid_escape = next(
        d for d in invalid_escape_json["diagnostics"] if d["code"] == "E0103")
    assert invalid_escape["details"] == "invalid escape sequence `\\q`", invalid_escape
    assert invalid_escape["position"]["end_byte"] == (
        invalid_escape["position"]["start_byte"] + 2), invalid_escape
    invalid_escape_terminal = run(path, "terminal", command="check")
    assert "invalid escape sequence `\\q`" in invalid_escape_terminal, invalid_escape_terminal
    invalid_escape_sarif = run(path, "sarif", command="check")
    invalid_escape_result = next(
        r for r in invalid_escape_sarif["runs"][0]["results"]
        if r["ruleId"] == "E0103")
    assert invalid_escape_result["locations"][0]["message"]["text"] == (
        "invalid escape sequence `\\q`"), invalid_escape_result
    assert invalid_escape_result["properties"]["phase"] == "lexer", invalid_escape_result

    path.write_text("proc main() -> int { let value = 'a; }")
    invalid_character_json = run(path, command="check")
    invalid_character = next(
        d for d in invalid_character_json["diagnostics"] if d["code"] == "E0101")
    assert invalid_character["details"] == "invalid character `'a`", invalid_character
    assert invalid_character["suggestions"], invalid_character
    assert invalid_character["suggestions"][0]["replacement"] == "'", invalid_character
    invalid_character_sarif = run(path, "sarif", command="check")
    invalid_character_result = next(
        r for r in invalid_character_sarif["runs"][0]["results"]
        if r["ruleId"] == "E0101")
    assert invalid_character_result["fixes"], invalid_character_result
    assert invalid_character_result["fixes"][0]["artifactChanges"], invalid_character_result

    path.write_text("use sample.lib;\nproc main() -> int { return 1; }\n")
    import_path = next(d for d in run(path, command="check")["diagnostics"]
                       if d["code"] == "E0301")
    assert "use `::` between import path segments" in import_path["details"], import_path
    missing = Path(directory) / "missing.vit"
    source_help = help_for(run(missing, command="check"), "E0005")
    assert str(missing) in source_help, source_help

    module_dir = Path(directory) / "imports" / "sample"
    module_dir.mkdir(parents=True)
    library = module_dir / "lib.vit"
    main = module_dir.parent / "main.vit"
    library.write_text(
        "space sample::lib;\n"
        "const private_value: i64 = 1;\n"
        "export const public_value: i64 = 2;\n")
    main.write_text(
        "space sample::main;\n"
        "use sample::lib;\n"
        "proc main() -> i64 { return lib::private_value; }\n")
    import_json = run(main)
    import_diagnostic = next(
        d for d in import_json["diagnostics"] if d["code"] == "E0404")
    import_help = next(
        a["message"] for a in import_diagnostic["annotations"]
        if a["kind"] == "help")
    assert "private_value" in import_help, import_help
    assert import_diagnostic["related_locations"], import_diagnostic
    assert import_diagnostic["related_locations"][0]["span"]["source"] == str(library), import_diagnostic
    import_terminal = run(main, "terminal")
    assert import_help in import_terminal, import_terminal
    assert str(library) in import_terminal, import_terminal
    import_sarif = run(main, "sarif")
    import_result = next(
        r for r in import_sarif["runs"][0]["results"] if r["ruleId"] == "E0404")
    assert import_result["properties"]["help"] == import_help, import_result
    assert import_result["relatedLocations"], import_result
    assert import_result["relatedLocations"][0]["physicalLocation"][
        "artifactLocation"]["uri"] == str(library), import_result

    collision = module_dir.parent / "collision.vit"
    collision.write_text(
        "space sample::collision;\n"
        "const public_value: i64 = 1;\n"
        "use sample::lib::*;\n"
        "proc main() -> i64 { return public_value; }\n")
    collision_json = run(collision)
    collision_diagnostic = next(
        d for d in collision_json["diagnostics"] if d["code"] == "E0402")
    assert collision_diagnostic["related_locations"][0]["span"][
        "source"] == str(library), collision_diagnostic
    collision_help = next(
        a["message"] for a in collision_diagnostic["annotations"]
        if a["kind"] == "help")
    assert "public_value" in collision_help, collision_help
    assert collision_help in run(collision, "terminal"), collision_help
    collision_sarif = run(collision, "sarif")
    collision_result = next(
        r for r in collision_sarif["runs"][0]["results"] if r["ruleId"] == "E0402")
    assert collision_result["properties"]["help"] == collision_help, collision_result
    assert collision_result["relatedLocations"][0]["physicalLocation"][
        "artifactLocation"]["uri"] == str(library), collision_result

print("Contextual diagnostic help: CLI, JSON, SARIF and version checks passed.")
