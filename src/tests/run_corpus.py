#!/usr/bin/env python3
"""Run the C compiler corpus with explicit fixture expectations."""

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "src/tests"
MANIFEST = TESTS / "corpus_expectations.tsv"


def unavailable_reason(value):
    _, separator, reason = value.partition("|")
    return reason if separator else value


def load_expectations():
    expectations = {}
    for number, line in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] not in {
            "negative",
            "unavailable",
            "obsolete",
        }:
            raise ValueError(f"{MANIFEST}:{number}: invalid expectation")
        kind, path, value = fields
        if path in expectations or not value:
            raise ValueError(f"{MANIFEST}:{number}: duplicate or empty expectation")
        expectations[path] = kind, value
    return expectations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="fixture checked separately by another compiler",
    )
    parser.add_argument("--include-unavailable", action="store_true")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    expectations = load_expectations()
    files = sorted(path.relative_to(ROOT).as_posix() for path in TESTS.rglob("*.vit"))
    present = set(files)
    excluded = set(args.exclude)
    unknown_excluded = excluded - present
    if unknown_excluded:
        raise ValueError(
            "excluded fixtures do not exist: " + ", ".join(sorted(unknown_excluded))
        )
    missing = set(expectations) - present
    stale = {
        path
        for path in missing
        if expectations[path][0] not in {"unavailable", "obsolete"}
    }
    if stale:
        raise ValueError(f"expectations refer to missing fixtures: {', '.join(sorted(stale))}")

    files = [path for path in files if path not in excluded]
    checked = negative = known_gaps = failed = 0
    unavailable_missing = sorted(missing - stale)
    skipped = len(unavailable_missing) + len(excluded)
    for path in sorted(excluded):
        print(f"[SKIP] {path}: checked separately by the native compiler")
    for path in unavailable_missing:
        _, reason = expectations[path]
        print(
            f"[SKIP] {path}: fixture is absent "
            f"({unavailable_reason(reason)})"
        )
    obsolete_present = 0

    for path in files:
        kind, value = expectations.get(path, ("positive", ""))
        if kind == "obsolete":
            obsolete_present += 1
            skipped += 1
            print(f"[SKIP] {path}: obsolete fixture ({value})")
            continue
        if (
            kind == "unavailable"
            and "|" not in value
            and not args.include_unavailable
        ):
            skipped += 1
            print(f"[SKIP] {path}: {unavailable_reason(value)}")
            continue
        checked += 1
        command = [str(compiler), "check", path, "--quiet", "--no-color"]
        if kind == "negative" or (
            kind == "unavailable" and "|" in value
        ):
            command = [
                str(compiler),
                "compile",
                path,
                "--quiet",
                "--no-color",
                "--stop-after-sema",
            ]
        try:
            result = subprocess.run(
                command,
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=15,
            )
        except subprocess.TimeoutExpired:
            failed += 1
            print(f"[FAIL] {path} (compiler timed out after 15 seconds)")
            continue
        if kind == "negative":
            negative += 1
            ok = result.returncode == 1 and f"error[{value}]" in result.stderr
        elif kind == "unavailable":
            expected_code, separator, reason = value.partition("|")
            if separator:
                if result.returncode == 0:
                    print(f"[PASS] {path} (previously unavailable, now supported)")
                    continue
                ok = (
                    result.returncode == 1
                    and f"error[{expected_code}]" in result.stderr
                )
                if ok:
                    known_gaps += 1
                    print(f"[KNOWN-GAP] {path} ({expected_code}): {reason}")
                    continue
            else:
                ok = result.returncode == 0
        else:
            ok = result.returncode == 0
        if ok:
            print(f"[PASS] {path} ({kind})")
        else:
            failed += 1
            print(f"[FAIL] {path} ({kind}, exit {result.returncode})")
            if result.stderr:
                print(result.stderr.rstrip())
    print(
        f"\nChecked: {checked}  Negative: {negative}  "
        f"Known gaps: {known_gaps}  Obsolete: {obsolete_present}  "
        f"Skipped: {skipped}  Failed: {failed}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
