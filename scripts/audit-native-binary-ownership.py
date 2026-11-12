#!/usr/bin/env python3
"""Audit bootstrap-owned symbols that enter the standard Vitte executable."""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"

EXPECTED_LINK_OBJECTS = (
    "$(NATIVE_PLATFORM_OBJECT)",
    "$(NATIVE_FILESYSTEM_OBJECT)",
    "$(NATIVE_HOST_OBJECT)",
    "$(NATIVE_IO_OBJECT)",
    "$(NATIVE_MAIN_OBJECT)",
)

EXPECTED_FILESYSTEM_EXPORTS = {
    "vitte_filesystem_buffer_destroy",
    "vitte_filesystem_canonicalize",
    "vitte_filesystem_create_directories",
    "vitte_filesystem_create_directory",
    "vitte_filesystem_current_directory",
    "vitte_filesystem_directory_list_destroy",
    "vitte_filesystem_error_name",
    "vitte_filesystem_exists",
    "vitte_filesystem_is_directory",
    "vitte_filesystem_is_file",
    "vitte_filesystem_is_symlink",
    "vitte_filesystem_join",
    "vitte_filesystem_kind_name",
    "vitte_filesystem_list_directory",
    "vitte_filesystem_normalize",
    "vitte_filesystem_normalize_impl",
    "vitte_filesystem_path_hash",
    "vitte_filesystem_path_is_absolute",
    "vitte_filesystem_read_file",
    "vitte_filesystem_remove_directory",
    "vitte_filesystem_remove_file",
    "vitte_filesystem_rename",
    "vitte_filesystem_stat",
    "vitte_filesystem_stats",
    "vitte_filesystem_stats_reset",
    "vitte_filesystem_write_file",
    "vitte_filesystem_write_file_atomic",
}

EXPECTED_FRONTEND_FILESYSTEM_IMPORTS = {
    "vitte_filesystem_create_directories",
    "vitte_filesystem_current_directory",
    "vitte_filesystem_directory_list_destroy",
    "vitte_filesystem_list_directory",
    "vitte_filesystem_remove_directory",
    "vitte_filesystem_remove_file",
    "vitte_filesystem_rename",
    "vitte_filesystem_stat",
}


def fail(message: str) -> None:
    raise SystemExit(f"[binary-ownership][error] {message}")


def normalized_symbol(symbol: str) -> str:
    # Mach-O prefixes external C symbols with an underscore; ELF and PE do not.
    return symbol[1:] if symbol.startswith("_") else symbol


def run_nm(nm: list[str], flags: list[str], path: Path) -> list[str]:
    try:
        result = subprocess.run(
            [*nm, *flags, str(path)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        fail(f"cannot run {' '.join(nm)}: {error}")
    if result.returncode != 0:
        fail(
            f"{' '.join(nm)} {' '.join(flags)} {path} failed: "
            f"{result.stderr.strip() or result.stdout.strip()}"
        )
    return result.stdout.splitlines()


def defined_symbols(nm: list[str], path: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run_nm(nm, ["-g"], path):
        fields = line.split()
        if len(fields) < 2:
            continue
        symbol_type = fields[-2]
        if len(symbol_type) != 1 or symbol_type.upper() == "U":
            continue
        symbols.add(normalized_symbol(fields[-1]))
    return symbols


def undefined_symbols(nm: list[str], path: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run_nm(nm, ["-u"], path):
        fields = line.split()
        if "U" in fields:
            symbol = fields[-1]
        elif len(fields) == 1:
            # Darwin nm prints one undefined symbol per line without a U tag.
            symbol = fields[0]
        else:
            continue
        symbols.add(normalized_symbol(symbol))
    return symbols


def check_link_manifest() -> None:
    text = MAKEFILE.read_text(encoding="utf-8")
    lines = text.splitlines()
    start = next(
        (index for index, line in enumerate(lines) if line.startswith("NATIVE_LINK_OBJECTS :=")),
        None,
    )
    if start is None:
        fail("NATIVE_LINK_OBJECTS manifest is missing")
    first_value = lines[start].split(":=", 1)[1].strip()
    values: list[str] = []
    continuation = first_value.endswith("\\")
    if first_value.removesuffix("\\").strip():
        values.append(first_value.removesuffix("\\").strip())
    index = start + 1
    while continuation and index < len(lines):
        value = lines[index].strip()
        continuation = value.endswith("\\")
        value = value.removesuffix("\\").strip()
        if value:
            values.append(value)
        index += 1
    if tuple(values) != EXPECTED_LINK_OBJECTS:
        fail(
            "standard link object inventory changed; classify each new owner "
            f"explicitly (found {values!r})"
        )


def check_symbols(nm: list[str], binary: Path, frontend_object: Path, filesystem_object: Path) -> None:
    for path in (binary, frontend_object, filesystem_object):
        if not path.is_file():
            fail(f"required build artifact is missing: {path}")

    filesystem_exports = {
        symbol
        for symbol in defined_symbols(nm, filesystem_object)
        if symbol.startswith("vitte_filesystem_")
    }
    if filesystem_exports != EXPECTED_FILESYSTEM_EXPORTS:
        missing = sorted(EXPECTED_FILESYSTEM_EXPORTS - filesystem_exports)
        added = sorted(filesystem_exports - EXPECTED_FILESYSTEM_EXPORTS)
        fail(f"bootstrap filesystem exports changed (missing={missing}, added={added})")

    frontend_imports = {
        symbol
        for symbol in undefined_symbols(nm, frontend_object)
        if symbol.startswith("vitte_filesystem_")
    }
    if frontend_imports != EXPECTED_FRONTEND_FILESYSTEM_IMPORTS:
        missing = sorted(EXPECTED_FRONTEND_FILESYSTEM_IMPORTS - frontend_imports)
        added = sorted(frontend_imports - EXPECTED_FRONTEND_FILESYSTEM_IMPORTS)
        fail(
            "Vitte frontend filesystem dependencies changed; review ownership "
            f"and tests (missing={missing}, added={added})"
        )

    executable_exports = defined_symbols(nm, binary)
    missing_legacy_exports = sorted(filesystem_exports - executable_exports)
    if missing_legacy_exports:
        fail(f"directly linked filesystem symbols are absent from the executable: {missing_legacy_exports}")
    missing_from_binary = sorted(frontend_imports - executable_exports)
    if missing_from_binary:
        fail(f"filesystem ABI imports are not defined by the executable: {missing_from_binary}")

    unreferenced = filesystem_exports - frontend_imports
    print("Main executable ownership audit passed:")
    print(f"  Vitte-generated frontend object: {frontend_object.relative_to(ROOT)}")
    print("  Native host/OS boundary objects: platform, native_host, native_io, native_main")
    print(
        "  Bootstrap filesystem C object: "
        f"{len(filesystem_exports)} public symbols; "
        f"{len(frontend_imports)} satisfy active Vitte frontend imports; "
        f"{len(unreferenced)} additional exports are linked but unreferenced there."
    )
    print("  Active filesystem imports: " + ", ".join(sorted(frontend_imports)))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", default="nm", help="nm-compatible symbol inspector")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--frontend-object", type=Path, required=True)
    parser.add_argument("--filesystem-object", type=Path, required=True)
    args = parser.parse_args()

    args.binary = args.binary.resolve()
    args.frontend_object = args.frontend_object.resolve()
    args.filesystem_object = args.filesystem_object.resolve()

    check_link_manifest()
    check_symbols(
        shlex.split(args.nm),
        args.binary,
        args.frontend_object,
        args.filesystem_object,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        print(f"[binary-ownership][error] {error}", file=sys.stderr)
        raise SystemExit(1)
