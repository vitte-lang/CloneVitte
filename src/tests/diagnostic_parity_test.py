"""Check that every diagnostic format exposes the same public metadata."""

from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
format_binary = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else binary
root = Path(__file__).resolve().parents[2]
registry = root / "src/bootstrap/diagnostic/registry.c"


def run_diagnostic(path, fmt):
    command = [
        str(format_binary),
        "check",
        str(path),
        "--quiet",
        "--no-color",
        f"--diagnostic={fmt}",
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 1, (
        command,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    if fmt == "terminal":
        return result.stderr
    try:
        return json.loads(result.stderr)
    except json.JSONDecodeError as error:
        raise AssertionError((fmt, result.stderr)) from error


def first_json_diagnostic(document, code):
    matches = [item for item in document["diagnostics"] if item["code"] == code]
    assert len(matches) == 1, (code, document)
    return matches[0]


def first_sarif_result(document, code):
    results = document["runs"][0]["results"]
    matches = [item for item in results if item["ruleId"] == code]
    assert len(matches) == 1, (code, document)
    return matches[0]


def first_lsp_diagnostic(document, code):
    matches = [item for item in document if item["code"] == code]
    assert len(matches) == 1, (code, document)
    return matches[0]


def check_explain_for_every_public_code():
    # registry.c is the authoritative source for the public code set.  Keep
    # this check at the CLI boundary so a code cannot be present in the
    # registry while being rejected by `vitte explain`.
    codes = []
    for code in re.findall(r'"([EWNH][0-9]{4})"', registry.read_text()):
        if code not in codes:
            codes.append(code)
    assert codes, "the diagnostic registry contains no public codes"

    for code in codes:
        text = subprocess.run(
            [str(binary), "explain", code],
            capture_output=True,
            text=True,
        )
        assert text.returncode == 0, (code, text.stdout, text.stderr)
        assert code in text.stdout, (code, text.stdout)

        structured = subprocess.run(
            [str(binary), "explain", code, "--diagnostic=json"],
            capture_output=True,
            text=True,
        )
        assert structured.returncode == 0, (
            code,
            structured.stdout,
            structured.stderr,
        )
        document = json.loads(structured.stdout)
        assert document["diagnostic"]["code"] == code, (code, document)
        assert document["diagnostic"]["explanation"], (code, document)
        assert document["diagnostic"]["guidance"], (code, document)


def check_format_parity():
    source = (
        "proc add(a: int) -> int { return a; }\n"
        "proc main() -> int { return add(\"42\"); }\n"
    )

    with tempfile.TemporaryDirectory(prefix="vitte-diagnostic-parity-") as directory:
        path = Path(directory) / "case.vit"
        path.write_text(source)

        terminal = run_diagnostic(path, "terminal")
        json_document = run_diagnostic(path, "json")
        sarif_document = run_diagnostic(path, "sarif")
        lsp_document = run_diagnostic(path, "lsp")

        code = "E0602"
        diagnostic = first_json_diagnostic(json_document, code)
        sarif = first_sarif_result(sarif_document, code)
        lsp = first_lsp_diagnostic(lsp_document, code)
        lsp_data = lsp["data"]
        context = diagnostic["context"]
        position = diagnostic["position"]
        properties = sarif["properties"]
        lsp_position = lsp_data["position"]

        # Public identity and diagnostic semantics must be stable across all
        # renderers.  LSP severity is numeric by protocol, so compare its
        # data mirror with the textual/structured severity name.
        assert diagnostic["code"] == sarif["ruleId"] == lsp["code"] == code
        assert diagnostic["severity"] == properties["severity"] == lsp_data["severity"] == "error"
        assert diagnostic["phase"] == properties["phase"] == lsp_data["phase"] == "type-check"
        assert diagnostic["category"] == properties["category"] == lsp_data["category"] == "call"
        assert context["expected_type"] == properties["expectedType"] == lsp_data["expectedType"] == "i64"
        assert context["actual_type"] == properties["actualType"] == lsp_data["actualType"] == "string"
        help_text = diagnostic["annotations"][0]["message"]
        assert properties["help"] == lsp_data["help"] == help_text
        assert diagnostic["causes"] == properties["causes"] == lsp_data["causes"] == []

        # JSON/SARIF retain source byte spans.  LSP retains the same raw span
        # in data.labels while exposing the converted UTF-16 range required by
        # the protocol.
        sarif_region = sarif["locations"][0]["physicalLocation"]["region"]
        primary_label = next(
            label for label in lsp_data["labels"] if label["style"] == "primary"
        )
        assert position["source"] == sarif["locations"][0]["physicalLocation"]["artifactLocation"]["uri"]
        assert position["source"] == primary_label["source"]
        assert lsp_position["source"] == position["source"]
        assert lsp_position["hasSpan"] == position["has_span"]
        assert lsp_position["startByte"] == position["start_byte"]
        assert lsp_position["endByte"] == position["end_byte"]
        assert lsp_position["startLine"] == position["start_line"]
        assert lsp_position["startColumn"] == position["start_column"]
        assert lsp_position["endLine"] == position["end_line"]
        assert lsp_position["endColumn"] == position["end_column"]
        assert position["start_byte"] == sarif_region["byteOffset"] == primary_label["startByte"]
        assert position["end_byte"] - position["start_byte"] == sarif_region["byteLength"]
        assert position["end_byte"] == primary_label["endByte"]
        assert sarif_region["startLine"] == position["start_line"] + 1
        assert sarif_region["startColumn"] == position["start_column"] + 1
        assert sarif_region["endLine"] == position["end_line"] + 1
        assert sarif_region["endColumn"] == position["end_column"] + 1
        assert lsp["range"]["start"]["line"] == position["start_line"]
        assert lsp["range"]["start"]["character"] == position["start_column"]
        assert lsp["range"]["end"]["line"] == position["end_line"]
        assert lsp["range"]["end"]["character"] == position["end_column"]

        # The terminal renderer must carry the same user-facing context.
        assert code in terminal
        assert "phase: type-check" in terminal
        assert "expected `i64`, found `string`" in terminal
        assert help_text in terminal
        assert str(path) in terminal

        # Related declaration locations are part of the parity contract too.
        related_json = diagnostic["related_locations"][0]["span"]
        related_sarif = sarif["relatedLocations"][0]["physicalLocation"]
        related_lsp = lsp["relatedInformation"][0]
        related_region = related_sarif["region"]
        assert related_json["source"] == related_sarif["artifactLocation"]["uri"]
        assert related_json["start_byte"] == related_region["byteOffset"]
        assert related_json["end_byte"] - related_json["start_byte"] == related_region["byteLength"]
        assert related_lsp["location"]["uri"] == related_json["source"]
        assert related_lsp["location"]["range"]["start"]["line"] == related_json["start_line"]
        assert related_lsp["location"]["range"]["start"]["character"] == related_json["start_column"]


check_explain_for_every_public_code()
check_format_parity()
print("Diagnostic parity: explain, terminal, JSON, SARIF and LSP passed.")
