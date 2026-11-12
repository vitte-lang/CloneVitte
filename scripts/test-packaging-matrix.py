#!/usr/bin/env python3
"""Validate package target inventory, aliases, and builder lookup behavior."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from verify_release_installer_inventory import (
    METADATA_FILES,
    required_artifacts,
    validate_release_inventory,
)


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "scripts" / "package-matrix.sh"
BUILDER = ROOT / "scripts" / "build-all-installers.sh"
TARGET_VERIFIER = ROOT / "scripts" / "verify-installer-target.py"


def run(*command: str) -> str:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"stdout={result.stdout!r}\nstderr={result.stderr!r}"
        )
    return result.stdout


def script_path(path: Path) -> str:
    if os.name != "nt":
        return str(path)
    result = subprocess.run(
        ["cygpath", "-u", str(path)], text=True, capture_output=True, timeout=10
    )
    if result.returncode != 0:
        raise AssertionError(f"cygpath failed for {path}: {result.stderr}")
    return result.stdout.strip()


def test_target_guard() -> None:
    host = sys.platform
    if host.startswith("darwin"):
        claimed_os, wrong_os = "macos", "linux"
    elif host.startswith("linux"):
        claimed_os, wrong_os = "linux", "macos"
    else:
        claimed_os, wrong_os = "windows", "linux"
    binary = ROOT / "VERSION"
    result = subprocess.run(
        [sys.executable, str(TARGET_VERIFIER), str(binary), wrong_os, "amd64"],
        cwd=ROOT, text=True, capture_output=True, timeout=10,
    )
    if result.returncode == 0 or "requires a" not in result.stderr:
        raise AssertionError("installer target guard accepted a foreign platform")


def test_release_inventory() -> None:
    version = "0.1.1"
    artifacts = required_artifacts(version)
    with tempfile.TemporaryDirectory(prefix="vitte-release-inventory-") as temporary:
        out = Path(temporary)
        index_entries = []
        for name, (os_name, arch) in artifacts.items():
            payload = f"fixture installer: {name}\n".encode()
            digest = hashlib.sha256(payload).hexdigest()
            (out / name).write_bytes(payload)
            (out / f"{name}.sha256").write_text(f"{digest}  {name}\n")
            (out / f"{name}.MANIFEST.json").write_text(
                json.dumps(
                    {
                        "name": name,
                        "version": version,
                        "os": os_name,
                        "arch": arch,
                        "sha256": digest,
                        "installed_commands": [
                            "vitte",
                            "vittec",
                            "vitte-installer-doctor",
                        ],
                        "contents": [
                            "compiler",
                            "runtime",
                            "stdlib",
                            "editors",
                            "system-completions",
                        ],
                    }
                )
            )
            index_entries.append(
                {
                    "name": name,
                    "version": version,
                    "os": os_name,
                    "arch": arch,
                    "sha256": digest,
                }
            )
        (out / "INSTALLERS.json").write_text(
            json.dumps(
                {
                    "schema": "org.vitte.installer-index.v1",
                    "version": version,
                    "artifacts": index_entries,
                }
            )
        )
        for metadata_name in METADATA_FILES - {"INSTALLERS.json"}:
            (out / metadata_name).write_text("fixture metadata\n")

        validate_release_inventory(out, version)

        missing_installer = next(iter(artifacts))
        (out / missing_installer).unlink()
        try:
            validate_release_inventory(out, version)
        except ValueError as error:
            if "missing release files" not in str(error):
                raise AssertionError(f"missing installer was misreported: {error}") from error
        else:
            raise AssertionError("release inventory accepted a missing installer")

        (out / missing_installer).write_bytes(b"restored fixture\n")
        stale = out / "vitte_0.1.0_amd64.deb"
        stale.write_bytes(b"old release installer\n")
        try:
            validate_release_inventory(out, version)
        except ValueError as error:
            if "stale 0.1.0 files" not in str(error):
                raise AssertionError(f"stale installer was misreported: {error}") from error
        else:
            raise AssertionError("release inventory accepted an old 0.1.0 installer")


def main() -> int:
    test_release_inventory()
    test_target_guard()
    matrix_script = script_path(MATRIX)
    builder_script = script_path(BUILDER)
    matrix = run("sh", matrix_script, "list")
    builder = run("sh", builder_script, "--list-targets")
    if matrix != builder:
        raise AssertionError("build-all-installers target list drifted from package-matrix.sh")

    rows = [tuple(line.split()) for line in matrix.splitlines() if line.strip()]
    if len(rows) < 40:
        raise AssertionError(f"package target inventory unexpectedly small: {len(rows)} rows")
    if any(len(row) != 3 for row in rows):
        raise AssertionError("each package target must contain OS, architecture, and format")
    if len(rows) != len(set(rows)):
        raise AssertionError("package target inventory contains duplicates")

    required = {
        ("linux", "amd64", "deb"),
        ("macos", "arm64", "pkg"),
        ("macos", "arm64", "dmg"),
        ("windows-11", "amd64", "exe"),
    }
    missing = required.difference(rows)
    if missing:
        raise AssertionError(f"representative native packages missing from matrix: {sorted(missing)}")

    for os_name, arch, package_format in rows:
        actual = run("sh", matrix_script, "lookup", os_name, arch, package_format).strip()
        if actual != " ".join((os_name, arch, package_format)):
            raise AssertionError(f"matrix lookup failed for {(os_name, arch, package_format)}: {actual!r}")

    aliases = {
        ("normalize-arch", "x86_64"): "amd64",
        ("normalize-arch", "aarch64"): "arm64",
        ("normalize-os", "Darwin"): "macos",
        ("normalize-os", "Windows"): "windows",
    }
    for (operation, value), expected in aliases.items():
        actual = run("sh", matrix_script, operation, value).strip()
        if actual != expected:
            raise AssertionError(f"{operation} {value!r}: expected {expected!r}, got {actual!r}")

    print(
        f"Package matrix passed: {len(rows)} unique targets, {len(aliases)} aliases, "
        "and the currently gated release installer inventory."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, AssertionError) as error:
        print(f"[packaging-matrix-test][error] {error}", file=sys.stderr)
        raise SystemExit(1)
