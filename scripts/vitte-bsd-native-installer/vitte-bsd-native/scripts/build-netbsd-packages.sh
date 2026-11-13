#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-netbsd-packages.sh [canonical builder options]

Builds the supported NetBSD portable tar.xz installer. This is not a native
pkgsrc package.
HELP
  exit 0
fi

exec sh "$SCRIPT_DIR/delegate-bsd-builder.sh" netbsd 10 "$@"
