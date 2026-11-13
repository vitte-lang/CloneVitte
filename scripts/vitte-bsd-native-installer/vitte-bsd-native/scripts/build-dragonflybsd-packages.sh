#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 1 ] && { [ "$1" = --help ] || [ "$1" = -h ]; }; then
  cat <<'HELP'
usage: build-dragonflybsd-packages.sh [canonical builder options]

Builds the supported DragonFly portable tar.xz installer. No native pkg(8)
package is claimed by this entry point.
HELP
  exit 0
fi

exec sh "$SCRIPT_DIR/delegate-bsd-builder.sh" dragonfly 6.4 "$@"
