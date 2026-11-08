#!/usr/bin/env python3
"""Verify that a core trap reaches program termination after successful compilation."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

try:
    import resource
except ImportError:  # Windows has no resource module.
    resource = None


def _disable_core_dumps() -> None:
    if resource is not None:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: trap_runtime_test.py COMPILER SOURCE", file=sys.stderr)
        return 2

    compiler = Path(sys.argv[1])
    source = Path(sys.argv[2])
    result = subprocess.run(
        [str(compiler), "run", str(source), "--quiet", "--no-color"],
        check=False,
        capture_output=True,
        text=True,
        preexec_fn=_disable_core_dumps if resource is not None else None,
    )
    diagnostics = result.stdout + result.stderr
    error_codes = re.findall(r"error\[(E\d{4})\]", diagnostics)
    terminated_after_build = (
        error_codes == ["E0012"]
        or (
            not error_codes
            and "generated program returned failure" in diagnostics
        )
    )
    if result.returncode == 0 or not terminated_after_build:
        print(
            f"core trap did not terminate after a successful build: "
            f"compiler={compiler} source={source} exit={result.returncode}\n"
            f"{diagnostics}",
            file=sys.stderr,
        )
        return 1

    print(f"core trap terminated as expected: {source.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
