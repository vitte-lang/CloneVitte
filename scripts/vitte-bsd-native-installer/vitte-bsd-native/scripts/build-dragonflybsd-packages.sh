#!/bin/sh
# Build a native DragonFly BSD pkg(8) archive for Vitte. Never installs to host.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
SCRIPT_NAME=build-dragonflybsd-packages
PREFIX=${PREFIX:-/usr/local}
PACKAGE_NAME=${PACKAGE_NAME:-vitte}
VERSION=${VERSION:-0.1.0}
MODE=${MODE:-full}
ARCH=${ARCH:-$(uname -m)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout/dragonflybsd}
PAYLOAD_SCRIPT=$ROOT_DIR/scripts/stage-installer-payload.sh
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT_DIR" log -1 --format=%ct 2>/dev/null || date +%s)}
KEEP_STAGE=${KEEP_STAGE:-0}
BOOTSTRAP=${BOOTSTRAP:-0}
DRY_RUN=0
LIST_TARGETS=0
PRINT_ENV=0

log() { printf '[%s] %s\n' "$SCRIPT_NAME" "$*"; }
die() { printf '[%s][error] %s\n' "$SCRIPT_NAME" "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "required command missing: $1"; }
usage() {
  cat <<'USAGE'
Usage: scripts/build-dragonflybsd-packages.sh [--dry-run] [--list-targets] [--print-env] [--help]
Environment:
  VERSION=0.1.0 MODE=full ARCH=amd64 PREFIX=/usr/local OUT_DIR=...
  BOOTSTRAP=1 STAGE1_CMD='...' STAGE2_CMD='...' STAGE1_BIN=... STAGE2_BIN=...
  VERIFY_CMD='...' KEEP_STAGE=1 SOURCE_DATE_EPOCH=...
Builds a native DragonFly BSD .pkg using pkg create; does not install it.
No cross-compilation. Bootstrap commands are explicit, never guessed.
USAGE
}
for arg in "$@"; do
  case "$arg" in
    --help|-h) usage; exit 0 ;;
    --dry-run) DRY_RUN=1 ;;
    --list-targets) LIST_TARGETS=1 ;;
    --print-env) PRINT_ENV=1 ;;
    *) die "unknown option: $arg" ;;
  esac
done
[ "$LIST_TARGETS" -eq 0 ] || { printf 'dragonflybsd amd64\n'; exit 0; }
case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac
case "$PREFIX" in /usr/local|/opt/vitte) ;; *) die "unsupported PREFIX: $PREFIX (use /usr/local or /opt/vitte)" ;; esac
case "$ARCH" in amd64|x86_64) ARCH=amd64 ;; *) die "DragonFly BSD native build currently supports amd64 only: $ARCH" ;; esac
[ "$MODE" = full ] || die "MODE=$MODE unsupported; use MODE=full"
case "$VERSION" in ''|*[!a-zA-Z0-9._+-]*) die 'invalid VERSION' ;; esac
case "$PACKAGE_NAME" in ''|*[!a-zA-Z0-9._+-]*) die 'invalid PACKAGE_NAME' ;; esac
case "$SOURCE_DATE_EPOCH" in ''|*[!0-9]*) die 'invalid SOURCE_DATE_EPOCH' ;; esac
if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'OS=DragonFly\nARCH=%s\nVERSION=%s\nPREFIX=%s\nMODE=%s\nOUT_DIR=%s\nBOOTSTRAP=%s\n' \
    "$ARCH" "$VERSION" "$PREFIX" "$MODE" "$OUT_DIR" "$BOOTSTRAP"
  exit 0
fi
if [ "$DRY_RUN" -eq 1 ]; then
  log "would build DragonFly BSD package $PACKAGE_NAME-$VERSION ($ARCH) in $OUT_DIR"
  exit 0
fi
[ "$(uname -s)" = DragonFly ] || die 'native DragonFly BSD host required; cross-compilation disabled'
need pkg; need python3; need tar; need mktemp; need find; need install; need sha256; need file
[ -x "$PAYLOAD_SCRIPT" ] || die "missing executable: $PAYLOAD_SCRIPT"
[ -d "$ROOT_DIR/editors" ] || die 'editors/ missing'
[ -d "$ROOT_DIR/completions" ] || die 'completions/ missing'
[ -f "$ROOT_DIR/assets/logo.png" ] || die 'assets/logo.png missing'
[ -f "$ROOT_DIR/scripts/vitte-editor-setup.py" ] || die 'scripts/vitte-editor-setup.py missing'

# Bootstrap only when the caller supplies the actual build graph and checks.
if [ "$BOOTSTRAP" = 1 ]; then
  for name in STAGE1_CMD STAGE2_CMD STAGE1_BIN STAGE2_BIN VERIFY_CMD; do
    eval 'value=${'"$name"'-}'
    [ -n "$value" ] || die "BOOTSTRAP=1 requires $name"
  done
  log 'building stage 1 (existing compiler remains untouched)'
  (cd "$ROOT_DIR" && sh -c "$STAGE1_CMD") || die 'stage 1 failed'
  [ -x "$STAGE1_BIN" ] || die 'stage 1 compiler missing'
  log 'building stage 2'
  (cd "$ROOT_DIR" && VITTE_STAGE1="$STAGE1_BIN" sh -c "$STAGE2_CMD") || die 'stage 2 failed'
  [ -x "$STAGE2_BIN" ] || die 'stage 2 compiler missing'
  "$STAGE1_BIN" --version >/dev/null || die 'stage 1 cannot execute'
  "$STAGE2_BIN" --version >/dev/null || die 'stage 2 cannot execute'
  (cd "$ROOT_DIR" && VITTE_BIN="$STAGE2_BIN" sh -c "$VERIFY_CMD") || die 'bootstrap regression tests failed'
elif [ "$BOOTSTRAP" != 0 ]; then
  die 'BOOTSTRAP must be 0 or 1'
fi

mkdir -p "$OUT_DIR"
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/vitte-dragonfly.XXXXXXXX")
cleanup() {
  if [ "$KEEP_STAGE" = 1 ]; then log "staging preserved: $STAGE"; else rm -rf "$STAGE"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ROOTFS=$STAGE/root
DEST=$ROOTFS$PREFIX
mkdir -p "$DEST"

# Existing Vitte staging contract: rootfs, target OS, arch, ABI family.
# Refuse incompatible payloads instead of silently shipping Linux binaries.
log 'staging Vitte compiler, runtime, standard library and modules'
VERSION=$VERSION SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH \
  "$PAYLOAD_SCRIPT" "$ROOTFS" dragonflybsd "$ARCH" unix || \
  die 'payload staging failed (check DragonFly target support in stage-installer-payload.sh)'

# Some staging implementations use the FreeBSD target name. This must be
# explicitly selected and ABI-validated by the project, not silently assumed.
[ -x "$DEST/bin/vitte" ] || die "staged compiler missing: $DEST/bin/vitte"
[ -x "$DEST/bin/vittec" ] || die "staged compiler missing: $DEST/bin/vittec"
[ -d "$DEST/share/vitte" ] || die 'staged Vitte resources missing'

copy_dir() {
  src=$1; dst=$2
  [ -d "$src" ] || return 0
  mkdir -p "$dst"
  (cd "$src" && tar -cf - .) | (cd "$dst" && tar -xf -)
}
copy_file() {
  src=$1; dst=$2
  [ -f "$src" ] || return 0
  mkdir -p "$(dirname "$dst")"
  install -m 0644 "$src" "$dst"
}

log 'installing complete editor and completion resources'
copy_dir "$ROOT_DIR/editors" "$DEST/share/vitte/editors"
copy_dir "$ROOT_DIR/completions" "$DEST/share/vitte/completions"
copy_file "$ROOT_DIR/assets/logo.png" "$DEST/share/vitte/assets/logo.png"
install -m 0755 "$ROOT_DIR/scripts/vitte-editor-setup.py" "$DEST/bin/vitte-editor-setup"

# BSD package-managed runtime paths: no files under /usr/share or /etc.
for editor in vim neovim; do
  case "$editor" in
    vim) runtime=$DEST/share/vim/vimfiles ;;
    neovim) runtime=$DEST/share/nvim/site ;;
  esac
  for part in syntax ftdetect ftplugin indent compiler colors after; do
    copy_dir "$ROOT_DIR/editors/vim/$part" "$runtime/$part"
  done
  copy_file "$ROOT_DIR/editors/vim/ultisnips/vitte.snippets" "$runtime/UltiSnips/vitte.snippets"
done

# Ensure filetype detection works even when repository has no ftdetect file.
if [ ! -f "$DEST/share/vim/vimfiles/ftdetect/vitte.vim" ]; then
  mkdir -p "$DEST/share/vim/vimfiles/ftdetect"
  cat > "$DEST/share/vim/vimfiles/ftdetect/vitte.vim" <<'VIM'
augroup vitte_filetype_detection
  autocmd!
  autocmd BufRead,BufNewFile *.vit,*.vitl,*.vitte setfiletype vitte
augroup END
VIM
fi
copy_file "$DEST/share/vim/vimfiles/ftdetect/vitte.vim" "$DEST/share/nvim/site/ftdetect/vitte.vim"
copy_file "$ROOT_DIR/editors/nano/vitte.nanorc" "$DEST/share/nano/vitte.nanorc"
for f in "$ROOT_DIR"/editors/emacs/*.el; do
  [ -f "$f" ] || continue
  copy_file "$f" "$DEST/share/emacs/site-lisp/vitte/$(basename "$f")"
done
if [ -f "$DEST/share/emacs/site-lisp/vitte/vitte-mode.el" ]; then
  cat > "$DEST/share/emacs/site-lisp/vitte/vitte-autoload.el" <<'ELISP'
;;; vitte-autoload.el --- Vitte mode registration
(add-to-list 'load-path (file-name-directory (or load-file-name buffer-file-name)))
(autoload 'vitte-mode "vitte-mode" "Vitte major mode." t)
(dolist (ext '("\\.vit\\'" "\\.vitl\\'" "\\.vitte\\'"))
  (add-to-list 'auto-mode-alist (cons ext 'vitte-mode)))
ELISP
fi
# Geany definitions are packaged, not forced into a user's configuration.
# Its extension mapping must be activated by vitte-editor-setup if supported.
for f in "$ROOT_DIR"/editors/geany/*.conf; do
  [ -f "$f" ] || continue
  copy_file "$f" "$DEST/share/vitte/geany/$(basename "$f")"
done
copy_file "$ROOT_DIR/editors/geany/filetypes.vitte.conf" "$DEST/share/geany/filedefs/filetypes.Vitte.conf"

# Completions: install only files supplied by the repository.
copy_file "$ROOT_DIR/completions/bash/vitte" "$DEST/share/bash-completion/completions/vitte"
copy_file "$ROOT_DIR/completions/fish/vitte.fish" "$DEST/share/fish/vendor_completions.d/vitte.fish"
copy_file "$ROOT_DIR/completions/zsh/_vitte" "$DEST/share/zsh/site-functions/_vitte"

# Validate staged executable ABI: 'file' is advisory; actual execution required.
log 'validating staged executables'
file "$DEST/bin/vitte" "$DEST/bin/vittec" > "$STAGE/binary-info.txt"
if grep -Ei 'ELF.*(Linux|GNU/Linux)' "$STAGE/binary-info.txt" >/dev/null; then
  die 'Linux ELF binary detected in DragonFly BSD package'
fi
"$DEST/bin/vitte" --version || die 'staged vitte does not run on DragonFly BSD'
"$DEST/bin/vittec" --version || die 'staged vittec does not run on DragonFly BSD'
[ -f "$DEST/share/vim/vimfiles/ftdetect/vitte.vim" ] || die 'Vim integration missing'
[ -f "$DEST/share/nano/vitte.nanorc" ] || die 'Nano integration missing'
[ -f "$DEST/share/emacs/site-lisp/vitte/vitte-mode.el" ] || die 'Emacs integration missing'
[ -f "$DEST/share/geany/filedefs/filetypes.Vitte.conf" ] || die 'Geany integration missing'
[ -f "$DEST/share/vitte/assets/logo.png" ] || die 'Vitte logo missing'
[ -f "$DEST/share/vitte/editors/tree-sitter/grammar.js" ] || die 'Tree-sitter grammar missing'
if [ -n "${VERIFY_CMD:-}" ]; then
  log 'running project regression verification'
  (cd "$ROOT_DIR" && VITTE_BIN="$DEST/bin/vitte" sh -c "$VERIFY_CMD") || die 'regression tests failed'
fi

# pkg(8) accepts a JSON manifest (UCL-compatible) with files indexed by
# absolute destination path. pkg create reads contents from ROOTFS.
log 'generating native pkg manifest'
python3 - "$DEST" "$PREFIX" "$PACKAGE_NAME" "$VERSION" "$ARCH" "$STAGE/manifest.json" "$SOURCE_DATE_EPOCH" <<'PY'
import json, pathlib, sys, os
root = pathlib.Path(sys.argv[1]); prefix, name, version, arch, output = sys.argv[2:7]
epoch = int(sys.argv[7]); files = {}
for p in sorted(root.rglob('*')):
    if p.is_file() or p.is_symlink():
        relative = p.relative_to(root).as_posix()
        files[prefix.rstrip('/') + '/' + relative] = '-'
        if not p.is_symlink():
            os.utime(p, (epoch, epoch))
if not files:
    raise SystemExit('empty package payload')
manifest = {
    'name': name, 'version': version, 'origin': 'lang/vitte',
    'comment': 'Vitte language compiler and complete development environment',
    'desc': 'Vitte compiler, runtime, modules, editor integrations and completions.',
    'maintainer': 'maintainers@vitte-lang.org',
    'www': 'https://vitte-lang.org/', 'prefix': prefix,
    'arch': 'dragonfly:' + arch, 'files': files,
}
pathlib.Path(output).write_text(json.dumps(manifest, indent=2) + '\n')
print(f'payload files: {len(files)}')
PY

# Use native pkg create rather than tarball masquerading as a package.
log 'building DragonFly BSD package'
pkg create -M "$STAGE/manifest.json" -r "$ROOTFS" -o "$OUT_DIR" || \
  die 'pkg create failed (check pkg version and DragonFly ABI metadata)'

# Find exact artifact(s) generated by pkg create, verify and checksum.
FOUND=0
for pkgfile in "$OUT_DIR"/"$PACKAGE_NAME"-"$VERSION"*.pkg "$OUT_DIR"/"$PACKAGE_NAME"-"$VERSION"*.txz; do
  [ -f "$pkgfile" ] || continue
  FOUND=1
  pkg info -F "$pkgfile" >/dev/null || die "invalid package: $pkgfile"
  (cd "$(dirname "$pkgfile")" && sha256 -r "$(basename "$pkgfile")" > "$(basename "$pkgfile").sha256")
  log "verified: $pkgfile"
  log "checksum: $pkgfile.sha256"
done
[ "$FOUND" -eq 1 ] || die "pkg create produced no identifiable package in $OUT_DIR"
log 'complete; install with pkg add <package-file> (as root)'
log 'rollback: use pkg install of the previously saved version; no live files were changed by this build'
