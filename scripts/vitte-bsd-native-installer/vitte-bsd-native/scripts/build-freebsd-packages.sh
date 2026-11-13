#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-freebsd-packages.sh [canonical builder options]

Delegates to the repository's FreeBSD pkg(8) package builder.
HELP
  exit 0
fi

exec sh "$SCRIPT_DIR/delegate-bsd-builder.sh" freebsd 14 "$@"
