#!/usr/bin/env python3
"""Create deterministic metadata for a directory of Vitte release installers."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


METADATA_NAMES = {
    "INSTALLERS.json",
    "CHECKSUMS.txt",
    "SIGNATURES.json",
    "SBOM.spdx.json",
    "SBOM.cyclonedx.json",
    "ATTESTATION.json",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_sidecar(name: str) -> bool:
    return name.endswith((".sha256", ".MANIFEST.json", ".sig", ".asc"))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-date-epoch", type=int, default=0)
    parser.add_argument("--sbom", action="store_true")
    parser.add_argument("--require-signatures", action="store_true")
    parser.add_argument("--public-key", type=Path)
    return parser.parse_args()


def verify_signature(artifact: Path, signature: Path, public_key: Path) -> None:
    subprocess.run(
        [
            "openssl",
            "dgst",
            "-sha256",
            "-verify",
            str(public_key),
            "-signature",
            str(signature),
            str(artifact),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    args = parse_arguments()
    out = args.out.resolve()
    if not out.is_dir():
        raise SystemExit(f"installer output directory not found: {out}")

    primary_paths = sorted(
        path
        for path in out.iterdir()
        if path.is_file()
        and path.name not in METADATA_NAMES
        and not is_sidecar(path.name)
    )
    if not primary_paths:
        raise SystemExit(f"no installer artifacts found in {out}")

    artifacts: list[dict[str, object]] = []
    signatures: list[dict[str, object]] = []
    signature_errors: list[str] = []
    for artifact in primary_paths:
        manifest_path = out / f"{artifact.name}.MANIFEST.json"
        if not manifest_path.is_file():
            raise SystemExit(f"missing artifact manifest: {manifest_path.name}")
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise SystemExit(f"invalid artifact manifest {manifest_path.name}: {error}")
        if manifest.get("name") != artifact.name:
            raise SystemExit(f"artifact name mismatch in {manifest_path.name}")
        if str(manifest.get("version")) != args.version:
            raise SystemExit(
                f"version mismatch in {manifest_path.name}: "
                f"{manifest.get('version')} != {args.version}"
            )

        digest = sha256(artifact)
        if manifest.get("sha256") != digest:
            raise SystemExit(f"artifact hash mismatch in {manifest_path.name}")
        entry = {
            "name": artifact.name,
            "version": args.version,
            "os": manifest.get("os"),
            "arch": manifest.get("arch"),
            "abi": manifest.get("abi"),
            "libc": manifest.get("libc"),
            "minimum_version": manifest.get("minimum_version"),
            "size": artifact.stat().st_size,
            "sha256": digest,
            "manifest": manifest_path.name,
        }
        artifacts.append(entry)

        signature_path = next(
            (
                candidate
                for candidate in (
                    out / f"{artifact.name}.sig",
                    out / f"{artifact.name}.asc",
                )
                if candidate.is_file()
            ),
            None,
        )
        verified = False
        if signature_path and args.public_key:
            try:
                verify_signature(artifact, signature_path, args.public_key)
                verified = True
            except (OSError, subprocess.CalledProcessError) as error:
                signature_errors.append(f"{artifact.name}: {error}")
        elif args.require_signatures:
            signature_errors.append(f"missing verifiable signature for {artifact.name}")

        signature_item: dict[str, object] = {
            "name": artifact.name,
            "verified": verified,
        }
        if signature_path:
            signature_item["signature"] = signature_path.name
        signatures.append(signature_item)

    if signature_errors:
        raise SystemExit("signature validation failed:\n" + "\n".join(signature_errors))

    timestamp = datetime.fromtimestamp(args.source_date_epoch, timezone.utc)
    generated = timestamp.strftime("%Y-%m-%dT%H:%M:%SZ")
    write_json(
        out / "INSTALLERS.json",
        {
            "schema": "org.vitte.installer-index.v1",
            "version": args.version,
            "generated_at": generated,
            "artifacts": artifacts,
        },
    )

    checksum_paths = sorted(
        path
        for path in out.iterdir()
        if path.is_file() and path.name not in METADATA_NAMES
    )
    (out / "CHECKSUMS.txt").write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in checksum_paths),
        encoding="utf-8",
    )

    all_verified = bool(signatures) and all(item["verified"] for item in signatures)
    write_json(
        out / "SIGNATURES.json",
        {
            "schema": "org.vitte.installer-signatures.v1",
            "all_verified": all_verified,
            "artifacts": signatures,
        },
    )

    if args.sbom:
        spdx_packages = []
        cyclonedx_components = []
        for artifact in artifacts:
            slug = re.sub(r"[^A-Za-z0-9.-]+", "-", str(artifact["name"]))
            spdx_packages.append(
                {
                    "SPDXID": f"SPDXRef-{slug}",
                    "name": artifact["name"],
                    "versionInfo": args.version,
                    "downloadLocation": "NOASSERTION",
                    "filesAnalyzed": False,
                    "checksums": [
                        {"algorithm": "SHA256", "checksumValue": artifact["sha256"]}
                    ],
                }
            )
            cyclonedx_components.append(
                {
                    "type": "file",
                    "bom-ref": str(artifact["name"]),
                    "name": artifact["name"],
                    "version": args.version,
                    "hashes": [
                        {"alg": "SHA-256", "content": artifact["sha256"]}
                    ],
                }
            )
        write_json(
            out / "SBOM.spdx.json",
            {
                "spdxVersion": "SPDX-2.3",
                "dataLicense": "CC0-1.0",
                "SPDXID": "SPDXRef-DOCUMENT",
                "name": f"Vitte {args.version} installers",
                "documentNamespace": f"https://vitte-lang.org/spdx/{args.version}",
                "creationInfo": {
                    "created": generated,
                    "creators": ["Tool: release_installer_metadata.py"],
                },
                "packages": spdx_packages,
            },
        )
        write_json(
            out / "SBOM.cyclonedx.json",
            {
                "bomFormat": "CycloneDX",
                "specVersion": "1.5",
                "version": 1,
                "metadata": {
                    "timestamp": generated,
                    "component": {
                        "type": "application",
                        "name": "Vitte",
                        "version": args.version,
                    },
                },
                "components": cyclonedx_components,
            },
        )

    print(f"[release_installer_metadata] indexed {len(artifacts)} installer artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
