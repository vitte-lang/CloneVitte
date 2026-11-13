#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-bsd-packages.sh [canonical builder options]

Dispatches FreeBSD to the native pkg(8) builder. Other supported BSD families
produce the validated portable tar.xz installer; a portable kit is not labelled
as a native OS package.
HELP
  exit 0
fi

exec sh "$SCRIPT_DIR/delegate-bsd-builder.sh" auto all "$@"
