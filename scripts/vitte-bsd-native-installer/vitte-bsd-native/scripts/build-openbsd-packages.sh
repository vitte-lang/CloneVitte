#!/bin/sh
# Vitte native OpenBSD package builder. Run on OpenBSD; no cross compilation.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-openbsd-packages
[ -f "$ROOT_DIR/scripts/common.sh" ] || { echo 'missing scripts/common.sh' >&2; exit 1; }
. "$ROOT_DIR/scripts/common.sh"
scripts_build_parse_common_flags "$@"
VERSION=${VERSION:-$(scripts_build_package_version)}
PACKAGE_NAME=${PACKAGE_NAME:-vitte}
ARCH=${ARCH:-native}
MODE=${MODE:-full}
PREFIX=${PREFIX:-/usr/local}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout/openbsd}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT_DIR" log -1 --format=%ct 2>/dev/null || date +%s)}
PAYLOAD_SCRIPT=$ROOT_DIR/scripts/stage-installer-payload.sh
EDITORS_DIR=$ROOT_DIR/editors
COMPLETIONS_DIR=$ROOT_DIR/completions
LOGO_FILE=$ROOT_DIR/assets/logo.png
BOOTSTRAP=${BOOTSTRAP:-0}
BOOTSTRAP_COMMAND=${BOOTSTRAP_COMMAND:-}
BOOTSTRAP_VERIFY_COMMAND=${BOOTSTRAP_VERIFY_COMMAND:-}
KEEP_STAGE=${KEEP_STAGE:-0}
case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac
say() { printf '[build-openbsd-packages] %s\n' "$*"; }
die() { say "ERROR: $*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"; }
copy_dir() { [ -d "$1" ] || die "missing directory: $1"; mkdir -p "$2"; cp -R "$1"/. "$2"/; }
copy_file() { [ -f "$1" ] || die "missing file: $1"; mkdir -p "$(dirname "$2")"; cp "$1" "$2"; chmod 0644 "$2"; }
first_file() { for candidate do [ ! -s "$candidate" ] || { printf '%s\n' "$candidate"; return 0; }; done; return 1; }
normalize_arch() {
 case "$1" in amd64|x86_64) echo amd64;; i386|i486|i586|i686) echo i386;; aarch64|arm64) echo arm64;; sparc64) echo sparc64;; powerpc|macppc) echo macppc;; riscv64) echo riscv64;; *) die "unsupported OpenBSD architecture: $1";; esac
}
scripts_build_maybe_help 'usage: build-openbsd-packages.sh [--dry-run] [--print-env] [--list-targets]'
if [ "$LIST_TARGETS" -eq 1 ]; then
 for target in amd64 i386 arm64 sparc64 macppc riscv64; do printf 'openbsd %s (native build required)\n' "$target"; done
 exit 0
fi
HOST_ARCH=$(normalize_arch "$(uname -m)")
if [ "$ARCH" = native ]; then ARCH=$HOST_ARCH; else ARCH=$(normalize_arch "$ARCH"); fi
if [ "$PRINT_ENV" -eq 1 ]; then
 for var in VERSION PACKAGE_NAME ARCH MODE PREFIX OUT_DIR BOOTSTRAP SOURCE_DATE_EPOCH; do eval "val=\${$var}"; printf '%s=%s\n' "$var" "$val"; done
 exit 0
fi
scripts_build_maybe_dry_run "would build OpenBSD package version=$VERSION arch=$ARCH prefix=$PREFIX out=$OUT_DIR mode=$MODE"
[ "$(uname -s)" = OpenBSD ] || die 'native build must run on OpenBSD'
[ "$ARCH" = "$HOST_ARCH" ] || die "cross-compilation disabled: requested $ARCH, host $HOST_ARCH"
[ "$MODE" = full ] || die 'only MODE=full is supported by this package profile'
case "$PREFIX" in /*) ;; *) die 'PREFIX must be absolute';; esac
case "$VERSION" in ''|*[!A-Za-z0-9._+-]*) die 'invalid package version';; esac
case "$PACKAGE_NAME" in ''|*[!A-Za-z0-9._+-]*) die 'invalid package name';; esac
[ -x "$PAYLOAD_SCRIPT" ] || die "missing executable staging script: $PAYLOAD_SCRIPT"
for tool in pkg_create pkg_info pkg_add tar find sort awk sed install python3 sha256; do require "$tool"; done
[ -d "$EDITORS_DIR" ] || die 'missing editors/'
[ -d "$COMPLETIONS_DIR" ] || die 'missing completions/'
[ -f "$LOGO_FILE" ] || die 'missing logo'
case "$BOOTSTRAP" in
 0) : ;;
 1)
   [ -n "$BOOTSTRAP_COMMAND" ] || die 'BOOTSTRAP=1 requires BOOTSTRAP_COMMAND'
   [ -n "$BOOTSTRAP_VERIFY_COMMAND" ] || die 'BOOTSTRAP=1 requires BOOTSTRAP_VERIFY_COMMAND'
   say 'running configured bootstrap command'
   (cd "$ROOT_DIR" && sh -c "$BOOTSTRAP_COMMAND")
   (cd "$ROOT_DIR" && sh -c "$BOOTSTRAP_VERIFY_COMMAND") || die 'bootstrap verification failed'
   ;;
 *) die 'BOOTSTRAP must be 0 or 1';;
esac
STAGE=$ROOT_DIR/target/installer-openbsd-$ARCH
DESTDIR=$STAGE/root
METADATA=$STAGE/metadata
STAGE_PREFIX=$DESTDIR$PREFIX
PKG_FILE=$OUT_DIR/${PACKAGE_NAME}-${VERSION}.tgz
mkdir -p "$OUT_DIR"
[ "$STAGE" != / ] || die 'unsafe stage path'
rm -rf "$STAGE"
mkdir -p "$STAGE_PREFIX" "$METADATA"
cleanup() { if [ "$KEEP_STAGE" != 1 ]; then rm -rf "$STAGE"; fi; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
say "staging compiler and libraries for OpenBSD $ARCH"
# The existing staging script uses /usr/local for Unix targets; isolate and relocate.
VERSION=$VERSION SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH "$PAYLOAD_SCRIPT" "$DESTDIR" openbsd "$ARCH" unix
if [ "$PREFIX" != /usr/local ] && [ -d "$DESTDIR/usr/local" ]; then
  copy_dir "$DESTDIR/usr/local" "$STAGE_PREFIX"
  rm -rf "$DESTDIR/usr/local"
fi
[ -x "$STAGE_PREFIX/bin/vitte" ] || die 'missing staged bin/vitte'
[ -x "$STAGE_PREFIX/bin/vittec" ] || die 'missing staged bin/vittec'
[ -d "$STAGE_PREFIX/share/vitte" ] || die 'missing staged standard library/modules'
SHARE=$STAGE_PREFIX/share/vitte
copy_dir "$EDITORS_DIR" "$SHARE/editors"
copy_dir "$COMPLETIONS_DIR" "$SHARE/completions"
copy_file "$LOGO_FILE" "$SHARE/assets/logo.png"
# Vim and Neovim use runtimepath under the OpenBSD prefix.
VIMROOT=$STAGE_PREFIX/share/vim/vimfiles
