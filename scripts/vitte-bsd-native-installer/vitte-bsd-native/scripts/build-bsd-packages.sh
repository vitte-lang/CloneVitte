#!/bin/sh
# Native Vitte package builder. Run on the target BSD host from the Vitte repository.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
OS=$(uname -s)
ARCH=$(uname -m)
VERSION=${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always 2>/dev/null | sed 's/^v//' || echo 0.1.0)}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/pkgout/bsd/$OS/$ARCH"}
STAGE=${STAGE:-"$ROOT_DIR/target/installer-bsd-$OS-$ARCH"}
PAYLOAD_SCRIPT="$ROOT_DIR/scripts/stage-installer-payload.sh"
MODE=${MODE:-full}
BOOTSTRAP=${BOOTSTRAP:-0}
PREFIX=${PREFIX:-}
fail() { printf 'build-bsd-packages: %s\n' "$*" >&2; exit 1; }
info() { printf 'build-bsd-packages: %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }
case "$OS" in
 FreeBSD|DragonFly) PREFIX=${PREFIX:-/usr/local}; FORMAT=pkg ;;
 OpenBSD) PREFIX=${PREFIX:-/usr/local}; FORMAT=OpenBSD ;;
 NetBSD) PREFIX=${PREFIX:-/usr/pkg}; FORMAT=NetBSD ;;
 *) fail "native BSD host required (found $OS)" ;;
esac
case "$PREFIX" in /*) : ;; *) fail 'PREFIX must be absolute' ;; esac
case "$PREFIX" in /|/usr|/etc|/bin|/sbin) fail 'unsafe PREFIX' ;; esac
[ "$MODE" = full ] || fail 'this builder supports MODE=full only'
[ -x "$PAYLOAD_SCRIPT" ] || fail "missing executable $PAYLOAD_SCRIPT"
need tar; need find; need python3; need mktemp
if [ "$BOOTSTRAP" = 1 ]; then
  [ -n "${BOOTSTRAP_CMD:-}" ] || fail 'BOOTSTRAP=1 requires BOOTSTRAP_CMD (real repository bootstrap command)'
  info 'running requested bootstrap before staging'
  (cd "$ROOT_DIR" && sh -c "$BOOTSTRAP_CMD") || fail 'bootstrap failed; installed compiler untouched'
fi
mkdir -p "$OUT_DIR"
# Keep a private, isolated package root. Never stage into the live prefix.
rm -rf "$STAGE"
DEST="$STAGE/root$PREFIX"
mkdir -p "$DEST"
# Shared Vitte staging interface, same as Debian builder: root, OS, architecture, ABI.
VERSION="$VERSION" "$PAYLOAD_SCRIPT" "$STAGE/root" "$(printf '%s' "$OS" | tr '[:upper:]' '[:lower:]')" "$ARCH" unix
# The payload script may use /usr/local or /usr/pkg. Require the expected prefix.
[ -x "$DEST/bin/vitte" ] || fail "payload missing $DEST/bin/vitte; inspect stage-installer-payload.sh target mapping"
# Include editor integrations and shell completions as package-owned resources.
mkdir -p "$DEST/share/vitte"
[ -d "$ROOT_DIR/editors" ] && (cd "$ROOT_DIR" && tar -cf - editors) | (cd "$DEST/share/vitte" && tar -xf -)
if [ -d "$ROOT_DIR/completions" ]; then
  (cd "$ROOT_DIR" && tar -cf - completions) | (cd "$DEST/share/vitte" && tar -xf -)
fi
# Install Vim runtime paths directly, in addition to archived sources.
if [ -d "$ROOT_DIR/editors/vim" ]; then
  mkdir -p "$DEST/share/vim/vimfiles"
  for dir in syntax ftdetect ftplugin indent compiler colors after; do
    if [ -d "$ROOT_DIR/editors/vim/$dir" ]; then
      (cd "$ROOT_DIR/editors/vim" && tar -cf - "$dir") | (cd "$DEST/share/vim/vimfiles" && tar -xf -)
    fi
  done
fi
if [ -f "$ROOT_DIR/editors/nano/vitte.nanorc" ]; then
  mkdir -p "$DEST/share/nano"
  cp "$ROOT_DIR/editors/nano/vitte.nanorc" "$DEST/share/nano/vitte.nanorc"
fi
if [ -d "$ROOT_DIR/editors/emacs" ]; then
  mkdir -p "$DEST/share/emacs/site-lisp/vitte"
  find "$ROOT_DIR/editors/emacs" -maxdepth 1 -type f -name '*.el' -exec cp '{}' "$DEST/share/emacs/site-lisp/vitte/" \;
fi
if [ -f "$ROOT_DIR/editors/geany/filetypes.vitte.conf" ]; then
  mkdir -p "$DEST/share/vitte/geany"
  cp "$ROOT_DIR/editors/geany/filetypes.vitte.conf" "$DEST/share/vitte/geany/"
fi
# Optional build verification: no install or package manager mutations.
"$DEST/bin/vitte" --version || fail 'staged compiler cannot run on this BSD host'
if [ -n "${VERIFY_CMD:-}" ]; then
  (cd "$ROOT_DIR" && VITTE_BIN="$DEST/bin/vitte" sh -c "$VERIFY_CMD") || fail 'regression validation failed'
fi
mkdir -p "$OUT_DIR"
case "$FORMAT" in
 pkg)
  need pkg
  MANIFEST="$STAGE/manifest.json"
  python3 - "$DEST" "$PREFIX" "$VERSION" "$MANIFEST" <<'PY'
import json, os, pathlib, sys
root, prefix, version, manifest = sys.argv[1:]
files = {}
for base, dirs, names in os.walk(root):
    for name in names:
        p = pathlib.Path(base) / name
        rel = p.relative_to(root).as_posix()
        files[prefix.rstrip('/') + '/' + rel] = '-'
assert files, 'empty package'
obj = {'name':'vitte','version':version,'origin':'lang/vitte',
       'comment':'Vitte compiler and full editor integrations',
       'desc':'Vitte compiler, runtime, modules, and editor resources',
       'maintainer':'vitte-lang project',
       'www':'https://github.com/vitte-lang/vitte',
       'prefix':prefix,'files':files}
with open(manifest,'w') as f: json.dump(obj,f,indent=2)
PY
  pkg create -M "$MANIFEST" -r "$STAGE/root" -o "$OUT_DIR"
  ;;
 OpenBSD|NetBSD)
  # pkg_create on these systems needs release-specific staging flags and
  # metadata. Never point it at the live filesystem by mistake.
  fail "$OS package creation needs validated pkg_create staging flags for this release; payload prepared at $DEST, no unsafe package produced"
  ;;
esac
info "packages: $OUT_DIR"
info "staging: $STAGE"
