#!/bin/sh
set -eu

VITTE_BIN=${VITTE_BIN:-vitte}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VITTE_BOOTSTRAP_BIN=${VITTE_BOOTSTRAP_BIN:-$REPO_ROOT/build/bin/vitte-bootstrap}
temporary_workdir=0
if [ -z "${WORKDIR:-}" ]; then
  WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/vitte-package-install-smoke.XXXXXX")
  temporary_workdir=1
fi
cleanup() {
  if [ "$temporary_workdir" -eq 1 ]; then rm -rf -- "$WORKDIR"; fi
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$WORKDIR"
case "$VITTE_BIN" in
  /*) ;;
  */*)
    vitte_dir=$(CDPATH= cd -- "$(dirname "$VITTE_BIN")" && pwd)
    VITTE_BIN=$vitte_dir/$(basename "$VITTE_BIN")
    ;;
esac
case "$VITTE_BOOTSTRAP_BIN" in
  /*) ;;
  */*)
    bootstrap_dir=$(CDPATH= cd -- "$(dirname "$VITTE_BOOTSTRAP_BIN")" && pwd)
    VITTE_BOOTSTRAP_BIN=$bootstrap_dir/$(basename "$VITTE_BOOTSTRAP_BIN")
    ;;
esac
[ -x "$VITTE_BOOTSTRAP_BIN" ] || {
  printf '[package-install-smoke][error] missing bootstrap compiler: %s\n' "$VITTE_BOOTSTRAP_BIN" >&2
  exit 1
}

cp "$REPO_ROOT/modules/json/tests/import_smoke.vit" "$WORKDIR/package-smoke.vit"
cd "$WORKDIR"

"$VITTE_BIN" install json
[ -s .vitte/modules/json/package.toml ]
[ -s .vitte/modules/json/parser/parser.vit ]
[ -s vitte.packages.toml ]
[ -s vitte.lock ]
grep -Eq '^dependency\.json = "[0-9]+\.[0-9]+\.[0-9]+"$' vitte.packages.toml
grep -Eq '^package\.json\.checksum = "sha256:[0-9a-f]{64}"$' vitte.lock

VITTE_MODULE_PATH="$WORKDIR/.vitte/modules" "$VITTE_BOOTSTRAP_BIN" check package-smoke.vit
VITTE_MODULE_PATH="$WORKDIR/.vitte/modules" "$VITTE_BOOTSTRAP_BIN" compile package-smoke.vit -o package-smoke
[ -x ./package-smoke ]
./package-smoke

printf '[package-install-smoke] OK bin=%s workdir=%s\n' "$VITTE_BIN" "$WORKDIR"
