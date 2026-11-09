#!/usr/bin/env python3
"""Verify that a Vitte release contains every installer exercised by CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tarfile
from pathlib import Path


METADATA_FILES = {
    "CHECKSUMS.txt",
    "INSTALLERS.json",
    "SIGNATURES.json",
    "SBOM.cyclonedx.json",
    "SBOM.spdx.json",
}


def required_artifacts(version: str) -> dict[str, tuple[str, str]]:
    """Return the complete native installer set built and installed by CI."""
    return {
        f"vitte_{version}_amd64.deb": ("linux", "amd64"),
        f"vitte-{version}-portable-linux-amd64.tar.gz": ("linux", "amd64"),
        f"vitte-{version}-macos-arm64.pkg": ("macos", "arm64"),
        f"vitte-{version}-macos-arm64.dmg": ("macos", "arm64"),
        f"vitte-{version}-windows-amd64-installer.exe": ("windows", "amd64"),
        f"vitte-{version}-windows-amd64-nsis.tar.gz": ("windows", "amd64"),
    }


def integration_artifact(version: str) -> str:
    return f"vitte-{version}-editors-completions.tar.gz"


def validate_release_inventory(out: Path, version: str) -> None:
    if not out.is_dir():
        raise ValueError(f"installer output directory not found: {out}")

    artifacts = required_artifacts(version)
    expected = set(METADATA_FILES)
    for name in (*artifacts, integration_artifact(version)):
        expected.update(
            {
                name,
                f"{name}.sha256",
                f"{name}.MANIFEST.json",
            }
        )

    actual = {path.name for path in out.iterdir() if path.is_file()}
    missing = sorted(expected - actual)
    stale = sorted(name for name in actual if "0.1.0" in name)
    unexpected = sorted(actual - expected)
    problems = []
    if missing:
        problems.append("missing release files: " + ", ".join(missing))
    if stale:
        problems.append("stale 0.1.0 files: " + ", ".join(stale))
    if unexpected:
        problems.append("unexpected release files: " + ", ".join(unexpected))
    if problems:
        raise ValueError("; ".join(problems))

    index = json.loads((out / "INSTALLERS.json").read_text(encoding="utf-8"))
    if index.get("schema") != "org.vitte.installer-index.v1":
        raise ValueError("INSTALLERS.json has an unsupported schema")
    if str(index.get("version")) != version:
        raise ValueError(
            f"INSTALLERS.json version is {index.get('version')!r}, expected {version!r}"
        )
    entries = {
        entry.get("name"): entry
        for entry in index.get("artifacts", [])
        if isinstance(entry, dict) and isinstance(entry.get("name"), str)
    }
    if set(entries) != set(artifacts) | {integration_artifact(version)}:
        raise ValueError(
            "INSTALLERS.json does not index the complete tested installer set"
        )

    for name, (expected_os, expected_arch) in artifacts.items():
        path = out / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        manifest_path = out / f"{name}.MANIFEST.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("name") != name or str(manifest.get("version")) != version:
            raise ValueError(f"{manifest_path.name} has the wrong artifact identity")
        if manifest.get("os") != expected_os or manifest.get("arch") != expected_arch:
            raise ValueError(
                f"{manifest_path.name} targets {manifest.get('os')}/{manifest.get('arch')}, "
                f"expected {expected_os}/{expected_arch}"
            )
        if manifest.get("sha256") != digest:
            raise ValueError(f"{manifest_path.name} SHA-256 does not match {name}")
        commands = set(manifest.get("installed_commands", []))
        if not {"vitte", "vittec", "vitte-installer-doctor"}.issubset(commands):
            raise ValueError(f"{manifest_path.name} is missing installed commands")
        contents = set(manifest.get("contents", []))
        if not {
            "compiler",
            "runtime",
            "stdlib",
            "editors",
            "system-completions",
        }.issubset(contents):
            raise ValueError(f"{manifest_path.name} does not describe a complete installer")

        sidecar = (out / f"{name}.sha256").read_text(encoding="utf-8").split()
        if len(sidecar) < 2 or sidecar[0] != digest or sidecar[-1] != name:
            raise ValueError(f"{name}.sha256 does not match {name}")
        entry = entries[name]
        if str(entry.get("version")) != version or entry.get("sha256") != digest:
            raise ValueError(f"INSTALLERS.json has invalid metadata for {name}")
        if entry.get("os") != expected_os or entry.get("arch") != expected_arch:
            raise ValueError(f"INSTALLERS.json has the wrong target for {name}")

    name = integration_artifact(version)
    digest = hashlib.sha256((out / name).read_bytes()).hexdigest()
    manifest = json.loads((out / f"{name}.MANIFEST.json").read_text(encoding="utf-8"))
    if (manifest.get("name"), manifest.get("version"), manifest.get("os"),
            manifest.get("arch"), manifest.get("sha256")) != (name, version, "any", "any", digest):
        raise ValueError(f"{name} has invalid integration metadata")
    if not {"editors", "system-completions"}.issubset(manifest.get("contents", [])):
        raise ValueError(f"{name} lacks editor or completion contents")
    if (out / f"{name}.sha256").read_text(encoding="utf-8").split() != [digest, name]:
        raise ValueError(f"{name}.sha256 does not match {name}")
    with tarfile.open(out / name, "r:gz") as archive:
        members = {member.name for member in archive.getmembers() if member.isfile()}
    if not any(path.startswith("editors/") for path in members) or not any(
        path.startswith("completions/") for path in members
    ):
        raise ValueError(f"{name} lacks editor or completion files")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        validate_release_inventory(args.out, args.version)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"[release-installer-inventory][error] {error}", file=sys.stderr)
        return 1
    print(
        f"[release-installer-inventory] verified {len(required_artifacts(args.version))} "
        "complete, CI-tested installers and release metadata"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
