#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SCRIPT_NAME=stage-installer-payload
. "$ROOT_DIR/scripts/common.sh"

if [ "${1:-}" = --dry-run ]; then
  shift
  printf '[stage-installer-payload][dry-run] native C payload: %s\n' "${1:-DEST}"
  exit 0
fi

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' \
    'usage: stage-installer-payload.sh [--dry-run] DEST PLATFORM ARCH [unix|windows]'
  exit 0
fi

DEST=${1:?missing destination}
PLATFORM=${2:?missing platform}
ARCH=${3:?missing architecture}
LAYOUT=${4:-unix}

VERSION=${VERSION:-$(scripts_build_package_version)}
VITTE_BIN=${VITTE_BIN:-$ROOT_DIR/build/bin/vitte}

#
# Build tool detection
#
# Vitte's Makefile uses GNU Make extensions.
# BSD make (/usr/bin/make on FreeBSD/GhostBSD) is therefore not suitable.
#

if [ -n "${MAKE:-}" ]; then
  MAKE_CMD=$MAKE
else
  OS_NAME=$(uname -s 2>/dev/null || printf '%s' unknown)

  case "$OS_NAME" in
    FreeBSD|OpenBSD|NetBSD|DragonFly)
      if command -v gmake >/dev/null 2>&1; then
        MAKE_CMD=$(command -v gmake)
      else
        scripts_build_die \
          "GNU Make is required on BSD. Install gmake first."
      fi
      ;;

    *)
      if command -v gmake >/dev/null 2>&1; then
        MAKE_CMD=$(command -v gmake)
      elif command -v make >/dev/null 2>&1; then
        MAKE_CMD=$(command -v make)
      else
        scripts_build_die "GNU Make was not found"
      fi
      ;;
  esac
fi

#
# Python detection
#

if command -v python3 >/dev/null 2>&1; then
  PYTHON=$(command -v python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON=$(command -v python)

  if ! "$PYTHON" -c 'import sys; raise SystemExit(0 if sys.version_info[0] >= 3 else 1)'; then
    scripts_build_die "Python 3 is required"
  fi
else
  scripts_build_die "Python 3 is required"
fi

#
# Build Vitte when necessary
#

if [ ! -x "$VITTE_BIN" ]; then
  printf '[stage-installer-payload] building Vitte with %s\n' "$MAKE_CMD"
  "$MAKE_CMD" -C "$ROOT_DIR" all
fi

verify_vitte "$VITTE_BIN" >/dev/null

#
# Installation layout
#

case "$LAYOUT" in
  unix)
    prefix=$DEST/usr/local

    mkdir -p \
      "$prefix/bin" \
      "$prefix/libexec/vitte" \
      "$prefix/share/vitte"

    for command in vitte vittec; do
      install -m 0755 \
        "$VITTE_BIN" \
        "$prefix/libexec/vitte/$command"

      {
        printf '%s\n' '#!/bin/sh'
        printf '%s\n' 'set -eu'
        printf '%s\n' 'self=$0'
        printf '%s\n' 'bindir=${self%/*}'
        printf '%s\n' \
          'case "$bindir" in /*) ;; *) bindir=$(CDPATH= cd -- "$bindir" && pwd) ;; esac'
        printf '%s\n' \
          'prefix=${VITTE_INSTALL_PREFIX:-${bindir%/bin}}'
        printf '%s\n' \
          'export VITTE_ROOT=${VITTE_ROOT:-$prefix/share/vitte}'
        printf 'exec "$prefix/libexec/vitte/%s" "$@"\n' "$command"
      } > "$prefix/bin/$command"

      chmod 0755 "$prefix/bin/$command"
    done

    install -m 0755 \
      "$ROOT_DIR/scripts/installer-doctor.sh" \
      "$prefix/bin/vitte-installer-doctor"
    ;;

  windows)
    prefix=$DEST

    mkdir -p \
      "$prefix/bin" \
      "$prefix/share/vitte"

    install -m 0755 \
      "$VITTE_BIN" \
      "$prefix/bin/vitte.exe"

    install -m 0755 \
      "$VITTE_BIN" \
      "$prefix/bin/vittec.exe"
    ;;

  *)
    scripts_build_die "unsupported layout: $LAYOUT"
    ;;
esac

#
# Shared resources
#

for directory in docs examples editors completions assets; do
  if [ -d "$ROOT_DIR/$directory" ]; then
    scripts_build_copy_tree \
      "$ROOT_DIR/$directory" \
      "$prefix/share/vitte/$directory"
  fi
done

mkdir -p "$prefix/share/vitte/tools"
install -m 0755 \
  "$ROOT_DIR/scripts/install-unix-integrations.sh" \
  "$prefix/share/vitte/tools/install-unix-integrations.sh"

#
# Public headers
#

if [ -d "$ROOT_DIR/include" ] &&
   find "$ROOT_DIR/include" \
     -type f \
     ! -name '.DS_Store' \
     -print -quit |
   grep -q .
then
  scripts_build_copy_tree \
    "$ROOT_DIR/include" \
    "$prefix/share/vitte/include"
fi

#
# Runtime
#

if [ -d "$ROOT_DIR/src/runtime" ] &&
   find "$ROOT_DIR/src/runtime" \
     -type f \
     ! -name '.DS_Store' \
     -print -quit |
   grep -q .
then
  scripts_build_require_file \
    "$ROOT_DIR/src/runtime/package.toml" \
    "Vitte runtime package metadata"

  scripts_build_require_file \
    "$ROOT_DIR/src/runtime/runtime.vit" \
    "Vitte runtime source"

  "$VITTE_BIN" check \
    "$ROOT_DIR/src/runtime/runtime.vit"

  scripts_build_copy_tree \
    "$ROOT_DIR/src/runtime" \
    "$prefix/share/vitte/runtime"
fi

#
# Modules / stdlib
#

scripts_build_install_modules \
  "$ROOT_DIR" \
  "$prefix/share/vitte"

scripts_build_verify_modules \
  "$prefix/share/vitte"

#
# Project metadata
#

for file in README.md LICENSE CHANGELOG.md VERSION; do
  if [ -f "$ROOT_DIR/$file" ]; then
    install -m 0644 \
      "$ROOT_DIR/$file" \
      "$prefix/share/vitte/$file"
  fi
done

#
# Installation manifest
#

"$PYTHON" - \
  "$prefix/share/vitte/INSTALLATION.json" \
  "$VERSION" \
  "$PLATFORM" \
  "$ARCH" <<'PY'
import json
import sys
from pathlib import Path

output = Path(sys.argv[1])

manifest = {
    "schema": "org.vitte.installation.v1",
    "name": "vitte",
    "version": sys.argv[2],
    "platform": sys.argv[3],
    "arch": sys.argv[4],
    "installed_commands": [
        "vitte",
        "vittec",
        "vitte-installer-doctor",
    ],
    "installed_modules": sorted(
        path.parent.name
        for path in (output.parent / "modules").glob("*/package.toml")
    ),
    "installed_components": {
        "include": (output.parent / "include").is_dir(),
        "runtime": (output.parent / "runtime").is_dir(),
        "modules": (output.parent / "modules").is_dir(),
        "stdlib": (output.parent / "stdlib").is_dir(),
    },
}

output.write_text(
    json.dumps(
        manifest,
        indent=2,
        sort_keys=True,
    ) + "\n",
    encoding="utf-8",
)
PY

printf \
  '[stage-installer-payload] staged native C compiler platform=%s arch=%s dest=%s version=%s make=%s\n' \
  "$PLATFORM" \
  "$ARCH" \
  "$DEST" \
  "$VERSION" \
  "$MAKE_CMD"

printf '[stage-installer-payload] staged native C compiler platform=%s arch=%s dest=%s version=%s\n' \
  "$PLATFORM" "$ARCH" "$DEST" "$VERSION"
