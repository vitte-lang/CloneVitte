#!/usr/bin/env python3
"""Package editor support and shell completions from this checkout."""

import argparse
import hashlib
import io
import json
import tarfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-date-epoch", required=True, type=int)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    expected = (root / "VERSION").read_text(encoding="utf-8").strip()
    if args.version != expected:
        parser.error(f"requested {args.version}, source tree is {expected}")
    paths = [p for directory in ("editors", "completions")
             for p in (root / directory).rglob("*") if p.is_file()]
    if not any(p.is_relative_to(root / "editors") for p in paths):
        parser.error("editor integrations are empty")
    if not any(p.is_relative_to(root / "completions") for p in paths):
        parser.error("shell completions are empty")
    args.out.mkdir(parents=True, exist_ok=True)
    name = f"vitte-{args.version}-editors-completions.tar.gz"
    artifact = args.out / name
    with artifact.open("wb") as output:
        import gzip
        with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=args.source_date_epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode="w") as archive:
                for path in sorted(paths):
                    data = path.read_bytes()
                    member = tarfile.TarInfo(path.relative_to(root).as_posix())
                    member.size = len(data)
                    member.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                    member.mtime = args.source_date_epoch
                    member.uid = member.gid = 0
                    member.uname = member.gname = ""
                    archive.addfile(member, io.BytesIO(data))
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    (args.out / f"{name}.sha256").write_text(f"{digest}  {name}\n", encoding="utf-8")
    manifest = {
        "name": name, "version": args.version, "os": "any", "arch": "any",
        "sha256": digest, "installed_commands": [],
        "contents": ["editors", "system-completions"],
    }
    (args.out / f"{name}.MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    with tarfile.open(artifact, "r:gz") as archive:
        names = {entry.name for entry in archive.getmembers() if entry.isfile()}
    if not any(name.startswith("editors/") for name in names) or not any(
        name.startswith("completions/") for name in names
    ):
        parser.error("archive lacks editors or completions")


if __name__ == "__main__":
    main()
