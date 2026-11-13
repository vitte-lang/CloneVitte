#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$SCRIPT_DIR
while [ "$ROOT_DIR" != / ]; do
  if [ -f "$ROOT_DIR/scripts/build-bsd-installers.sh" ] &&
     [ -f "$ROOT_DIR/scripts/common.sh" ]
  then
    break
  fi
  ROOT_DIR=$(dirname "$ROOT_DIR")
done

[ -f "$ROOT_DIR/scripts/build-bsd-installers.sh" ] || {
  printf '[build-ghostbsd-packages][error] Vitte repository root not found from %s\n' \
    "$SCRIPT_DIR" >&2
  exit 1
}

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-ghostbsd-packages.sh [--dry-run] [--list-targets] [--print-env]

Builds the supported GhostBSD portable installer through the canonical BSD
installer builder. The output is a tar.xz portable kit, not a native pkg(8)
package. Run on a GhostBSD host for a native GhostBSD compiler payload.
HELP
  exit 0
fi

ARCH=${ARCH:-amd64}
FAMILY=bsd
BSD_FAMILY=ghostbsd
export ARCH FAMILY BSD_FAMILY

exec sh "$ROOT_DIR/scripts/build-bsd-installers.sh" "$@"
