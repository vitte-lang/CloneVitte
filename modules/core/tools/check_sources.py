#!/usr/bin/env python3
"""Check every Core source file independently with the bootstrap frontend."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_sources.py COMPILER", file=sys.stderr)
        return 2

    root = Path(__file__).resolve().parents[3]
    compiler = Path(sys.argv[1]).resolve()
    source_root = root / "modules" / "core" / "src"
    module_paths = [source_root, root / "src"]
    existing_paths = os.environ.get("VITTE_MODULE_PATH", "")
    if existing_paths:
        module_paths.extend(Path(value) for value in existing_paths.split(os.pathsep) if value)
    environment = os.environ.copy()
    environment["VITTE_MODULE_PATH"] = os.pathsep.join(str(path) for path in module_paths)

    sources = sorted(
        path
        for pattern in ("*.vit", "*.vitte")
        for path in source_root.rglob(pattern)
    )
    accepted = 0
    rejected: list[tuple[Path, str]] = []
    for source in sources:
        result = subprocess.run(
            [
                str(compiler),
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
                "--no-color",
            ],
            cwd=root,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            accepted += 1
            continue
        diagnostics = (result.stdout + result.stderr).strip()
        rejected.append((source.relative_to(source_root), diagnostics))

    print(
        f"CORE_SOURCE_CHECK checked={len(sources)} "
        f"accepted={accepted} rejected={len(rejected)}"
    )
    for source, diagnostics in rejected:
        print(f"[CORE-SOURCE-REJECT] {source}")
        if diagnostics:
            print(diagnostics)
    return 1 if rejected else 0


if __name__ == "__main__":
    raise SystemExit(main())
