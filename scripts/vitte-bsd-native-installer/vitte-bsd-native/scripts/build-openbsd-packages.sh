#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-openbsd-packages.sh [canonical builder options]

Builds the supported OpenBSD portable tar.xz installer. This is not a native
OpenBSD pkg_add package.
HELP
  exit 0
fi

exec sh "$SCRIPT_DIR/delegate-bsd-builder.sh" openbsd 7.8 "$@"
