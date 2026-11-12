#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-linux-debs
. "$ROOT_DIR/scripts/common.sh"
scripts_build_parse_common_flags "$@"
VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}
case "$OUT_DIR" in
  /*) ;;
  *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;;
esac
ARCH=${ARCH:-all}
PACKAGE_NAME=${PACKAGE_NAME:-vitte}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT_DIR" log -1 --format=%ct 2>/dev/null || date +%s)}
EDITORS_DIR=$ROOT_DIR/editors
COMPLETIONS_DIR=$ROOT_DIR/completions
LOGO_FILE=$ROOT_DIR/assets/logo.png
PAYLOAD_SCRIPT=$ROOT_DIR/scripts/stage-installer-payload.sh
scripts_build_maybe_help \
  "usage: build-linux-debs.sh [--dry-run] [--list-targets] [--print-env]"
if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'VERSION=%s\n' "$VERSION"
  printf 'OUT_DIR=%s\n' "$OUT_DIR"
  printf 'ARCH=%s\n' "$ARCH"
  printf 'PACKAGE_NAME=%s\n' "$PACKAGE_NAME"
  printf 'SOURCE_DATE_EPOCH=%s\n' "$SOURCE_DATE_EPOCH"
  exit 0
fi
scripts_build_maybe_dry_run \
  "would build Linux deb packages version=$VERSION arch=$ARCH out=$OUT_DIR"
die() {
  scripts_build_die "$*"
}
require_directory() {
  directory=$1
  description=$2
  [ -d "$directory" ] ||
    die "$description directory not found: $directory"
  find "$directory" -type f -print -quit |
    grep -q . ||
    die "$description directory is empty: $directory"
}
find_first_file() {
  description=$1
  shift
  for candidate in "$@"; do
    if [ -f "$candidate" ] && [ -s "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  die "$description not found; checked: $*"
}
copy_tree() {
  source_dir=$1
  destination_dir=$2
  description=$3
  require_directory "$source_dir" "$description"
  scripts_build_copy_tree \
    "$source_dir" \
    "$destination_dir"
  printf '[build-linux-debs] added %s: %s\n' \
    "$description" \
    "$destination_dir"
}
add_archived_integrations() {
  data_root=$1
  share_root=$data_root/usr/local/share/vitte
  copy_tree \
    "$EDITORS_DIR" \
    "$share_root/editors" \
    "archived editor integrations"
  copy_tree \
    "$COMPLETIONS_DIR" \
    "$share_root/completions" \
    "shell completions"
}
install_vim_integration() {
  data_root=$1
  vim_syntax=$(find_first_file \
    "Vim Vitte syntax file" \
    "$EDITORS_DIR/vim/syntax/vitte.vim" \
    "$EDITORS_DIR/vim/vitte.vim" \
    "$EDITORS_DIR/vitte.vim")
  for vim_root in \
    "$data_root/usr/local/share/vim/vimfiles" \
    "$data_root/usr/share/vim/vimfiles"
  do
    mkdir -p \
      "$vim_root/syntax" \
      "$vim_root/ftdetect" \
      "$vim_root/ftplugin"
    install -m 0644 \
      "$vim_syntax" \
      "$vim_root/syntax/vitte.vim"
    if [ -f "$EDITORS_DIR/vim/ftdetect/vitte.vim" ]; then
      install -m 0644 \
        "$EDITORS_DIR/vim/ftdetect/vitte.vim" \
        "$vim_root/ftdetect/vitte.vim"
    else
      cat > "$vim_root/ftdetect/vitte.vim" <<'EOF'
augroup vitte_filetype_detection
  autocmd!
  autocmd BufRead,BufNewFile *.vit setfiletype vitte
  autocmd BufRead,BufNewFile *.vitte setfiletype vitte
  autocmd BufRead,BufNewFile *.vitl setfiletype vitte
augroup END
EOF
    fi
    if [ -f "$EDITORS_DIR/vim/ftplugin/vitte.vim" ]; then
      install -m 0644 \
        "$EDITORS_DIR/vim/ftplugin/vitte.vim" \
        "$vim_root/ftplugin/vitte.vim"
    else
      cat > "$vim_root/ftplugin/vitte.vim" <<'EOF'
if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1
setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal expandtab
setlocal shiftwidth=4
setlocal softtabstop=4
setlocal tabstop=4
EOF
    fi
    chmod 0644 \
      "$vim_root/syntax/vitte.vim" \
      "$vim_root/ftdetect/vitte.vim" \
      "$vim_root/ftplugin/vitte.vim"
  done
  # Install all additional Vim components supplied by the repository.
  for vim_root in \
    "$data_root/usr/local/share/vim/vimfiles" \
    "$data_root/usr/share/vim/vimfiles"
  do
    for component in indent compiler colors after; do
      if [ -d "$EDITORS_DIR/vim/$component" ]; then
        scripts_build_copy_tree \
          "$EDITORS_DIR/vim/$component" \
          "$vim_root/$component"
      fi
    done
    if [ -f "$EDITORS_DIR/vim/ultisnips/vitte.snippets" ]; then
      mkdir -p "$vim_root/UltiSnips"
      install -m 0644 "$EDITORS_DIR/vim/ultisnips/vitte.snippets" \
        "$vim_root/UltiSnips/vitte.snippets"
    fi
  done
  printf '%s\n' \
    '[build-linux-debs] installed Vim/Neovim integration'
}
install_emacs_integration() {
  data_root=$1
  emacs_mode=$(find_first_file \
    "Emacs Vitte mode" \
    "$EDITORS_DIR/emacs/vitte-mode.el" \
    "$EDITORS_DIR/emacs/vitte.el" \
    "$EDITORS_DIR/vitte-mode.el")
  local_root=$data_root/usr/local/share/emacs/site-lisp
  system_root=$data_root/usr/share/emacs/site-lisp/vitte
  startup_root=$data_root/etc/emacs/site-start.d
  mkdir -p \
    "$local_root" \
    "$system_root" \
    "$startup_root"
  install -m 0644 \
    "$emacs_mode" \
    "$local_root/vitte-mode.el"
  install -m 0644 \
    "$emacs_mode" \
    "$system_root/vitte-mode.el"
  if [ -f "$EDITORS_DIR/emacs/vitte-indent.el" ]; then
    install -m 0644 "$EDITORS_DIR/emacs/vitte-indent.el" \
      "$system_root/vitte-indent.el"
  fi
  if [ -d "$EDITORS_DIR/emacs/snippets" ]; then
    scripts_build_copy_tree "$EDITORS_DIR/emacs/snippets" \
      "$data_root/usr/share/vitte/emacs/snippets"
  fi
  cat > "$startup_root/50vitte.el" <<'EOF'
;;; 50vitte.el --- system-wide Vitte mode registration
(add-to-list 'load-path "/usr/local/share/emacs/site-lisp")
(add-to-list 'load-path "/usr/share/emacs/site-lisp/vitte")
(autoload 'vitte-mode
  "vitte-mode"
  "Major mode for the Vitte programming language."
  t)
(add-to-list 'auto-mode-alist '("\\\\.vit\\\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\\\.vitte\\\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\\\.vitl\\\\'" . vitte-mode))
(provide 'vitte-system-init)
;;; 50vitte.el ends here
EOF
  chmod 0644 "$startup_root/50vitte.el"
  printf '%s\n' \
    '[build-linux-debs] installed Emacs integration'
}
install_nano_integration() {
  data_root=$1
  nano_syntax=$(find_first_file \
    "Nano Vitte syntax file" \
    "$EDITORS_DIR/nano/vitte.nanorc" \
    "$EDITORS_DIR/nano/vitte.nano" \
    "$EDITORS_DIR/vitte.nanorc")
  for nano_root in \
    "$data_root/usr/share/nano" \
    "$data_root/usr/local/share/nano"
  do
    mkdir -p "$nano_root"
    install -m 0644 \
      "$nano_syntax" \
      "$nano_root/vitte.nanorc"
  done
  printf '%s\n' \
    '[build-linux-debs] installed Nano integration'
}
install_geany_integration() {
  data_root=$1
  geany_definition=$(find_first_file \
    "Geany Vitte filetype definition" \
    "$EDITORS_DIR/geany/filetypes.Vitte.conf" \
    "$EDITORS_DIR/geany/filetypes.vitte.conf" \
    "$EDITORS_DIR/geany/vitte.conf" \
    "$EDITORS_DIR/filetypes.Vitte.conf")
  for geany_root in \
    "$data_root/usr/share/geany/filedefs" \
    "$data_root/usr/local/share/geany/filedefs"
  do
    mkdir -p "$geany_root"
    install -m 0644 \
      "$geany_definition" \
      "$geany_root/filetypes.Vitte.conf"
  done
  # Store extension/snippet definitions as managed resources.
  # Do not overwrite the user's ~/.config/geany files from a Debian maintainer script.
  geany_resources="$data_root/usr/share/vitte/geany"
  mkdir -p "$geany_resources"
  for filename in filetype_extensions.conf filetypes.common snippets.vitte.conf; do
    if [ -f "$EDITORS_DIR/geany/$filename" ]; then
      install -m 0644 "$EDITORS_DIR/geany/$filename" \
        "$geany_resources/$filename"
    fi
  done
  printf '%s\n' \
    '[build-linux-debs] installed Geany integration'
}
install_language_tooling() {
  data_root=$1
  # These are source modules/resources, not executable editor plugins.
  for component in grammar semantic_tokens snippets tree-sitter; do
    if [ -d "$EDITORS_DIR/$component" ]; then
      scripts_build_copy_tree "$EDITORS_DIR/$component" \
        "$data_root/usr/local/share/vitte/editors/$component"
    fi
  done
}
install_shell_completions() {
  data_root=$1
  completion_root=$data_root/usr/local/share/vitte/completions
  bash_destination=$data_root/usr/share/bash-completion/completions/vitte
  fish_destination=$data_root/usr/share/fish/vendor_completions.d/vitte.fish
  zsh_destination=$data_root/usr/share/zsh/vendor-completions/_vitte

  mkdir -p \
    "$(dirname "$bash_destination")" \
    "$(dirname "$fish_destination")" \
    "$(dirname "$zsh_destination")"
  install -m 0644 "$completion_root/bash/vitte" "$bash_destination"
  install -m 0644 "$completion_root/fish/vitte.fish" "$fish_destination"
  install -m 0644 "$completion_root/zsh/_vitte" "$zsh_destination"
  printf '%s\n' \
    '[build-linux-debs] installed Bash, Fish and Zsh completions'
}
install_editor_integrations() {
  data_root=$1
  add_archived_integrations "$data_root"
  install_vim_integration "$data_root"
  install_emacs_integration "$data_root"
  install_nano_integration "$data_root"
  install_geany_integration "$data_root"
  install_language_tooling "$data_root"
  install_shell_completions "$data_root"
}
add_assets() {
  data_root=$1
  destination=$data_root/usr/local/share/vitte/assets
  mkdir -p "$destination"
  install -m 0644 \
    "$LOGO_FILE" \
    "$destination/logo.png"
  printf '[build-linux-debs] added Vitte logo: %s\n' \
    "$destination/logo.png"
}
verify_directory_not_empty() {
  directory=$1
  description=$2
  [ -d "$directory" ] ||
    die "$description directory missing: $directory"
  find "$directory" -type f -print -quit |
    grep -q . ||
    die "$description directory is empty: $directory"
}
verify_payload() {
  data_root=$1
  arch=$2
  [ -x "$data_root/usr/local/bin/vitte" ] ||
    die "missing or non-executable Vitte command for Linux $arch"
  [ -x "$data_root/usr/local/bin/vittec" ] ||
    die "missing or non-executable vittec command for Linux $arch"
  scripts_build_verify_modules "$data_root/usr/local/share/vitte"
  verify_directory_not_empty \
    "$data_root/usr/local/share/vitte/editors" \
    "archived editor integrations"
  verify_directory_not_empty \
    "$data_root/usr/local/share/vitte/completions" \
    "shell completions"
  scripts_build_require_file \
    "$data_root/usr/share/vim/vimfiles/syntax/vitte.vim" \
    "Vim Vitte syntax"
  scripts_build_require_file \
    "$data_root/usr/share/vim/vimfiles/ftdetect/vitte.vim" \
    "Vim Vitte filetype detection"
  scripts_build_require_file \
    "$data_root/usr/share/vim/vimfiles/ftplugin/vitte.vim" \
    "Vim Vitte filetype plugin"
  scripts_build_require_file \
    "$data_root/usr/share/emacs/site-lisp/vitte/vitte-mode.el" \
    "Emacs Vitte mode"
  scripts_build_require_file \
    "$data_root/etc/emacs/site-start.d/50vitte.el" \
    "Emacs Vitte automatic loader"
  scripts_build_require_file \
    "$data_root/usr/share/nano/vitte.nanorc" \
    "Nano Vitte syntax"
  scripts_build_require_file \
    "$data_root/usr/share/geany/filedefs/filetypes.Vitte.conf" \
    "Geany Vitte filetype"
  for required in \
    "$data_root/usr/share/vim/vimfiles/indent/vitte.vim" \
    "$data_root/usr/share/vim/vimfiles/compiler/vitte.vim" \
    "$data_root/usr/share/vim/vimfiles/UltiSnips/vitte.snippets" \
    "$data_root/usr/share/emacs/site-lisp/vitte/vitte-indent.el" \
    "$data_root/usr/share/vitte/geany/snippets.vitte.conf" \
    "$data_root/usr/local/share/vitte/editors/tree-sitter/grammar.js"; do
    scripts_build_require_file "$required" "Vitte editor integration"
  done
  scripts_build_require_file \
    "$data_root/usr/share/bash-completion/completions/vitte" \
    "Bash Vitte completion"
  scripts_build_require_file \
    "$data_root/usr/share/fish/vendor_completions.d/vitte.fish" \
    "Fish Vitte completion"
  scripts_build_require_file \
    "$data_root/usr/share/zsh/vendor-completions/_vitte" \
    "Zsh Vitte completion"
  scripts_build_require_file \
    "$data_root/usr/local/share/vitte/assets/logo.png" \
    "Vitte logo"
}
normalize_payload() {
  python3 - "$SOURCE_DATE_EPOCH" "$@" <<'PY'
import os
import sys
from pathlib import Path
timestamp = float(sys.argv[1])
for raw_path in sys.argv[2:]:
    path = Path(raw_path)
    paths = [path]
    if path.is_dir():
        paths.extend(sorted(path.rglob("*")))
    for item in paths:
        try:
            os.utime(item, (timestamp, timestamp), follow_symlinks=False)
        except FileNotFoundError:
            pass
PY
}
generate_md5sums() {
  data_root=$1
  output_file=$2
  scripts_build_require python3
  python3 - "$data_root" "$output_file" <<'PY'
import hashlib
import sys
from pathlib import Path
root = Path(sys.argv[1])
output = Path(sys.argv[2])
lines = []
for path in sorted(root.rglob("*")):
    if not path.is_file() or path.is_symlink():
        continue
    digest = hashlib.md5(path.read_bytes()).hexdigest()
    relative = path.relative_to(root).as_posix()
    lines.append(f"{digest}  {relative}")
output.write_text(
    "\n".join(lines) + ("\n" if lines else ""),
    encoding="utf-8",
)
PY
}
write_control_file() {
  control_root=$1
  arch=$2
  installed_size=$3
  cat > "$control_root/control" <<EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: devel
Priority: optional
Architecture: $arch
Maintainer: Vitte Team <maintainers@vitte-lang.org>
Depends: bash, python3, make, gcc, g++, clang, lld, binutils, git, pkg-config, cmake, ninja-build, gdb, nano, vim, neovim, emacs, geany, bash-completion, fish, zsh, curl, ca-certificates
Recommends: python3-venv, python3-pip
Provides: vitte-compiler, vitte-toolchain, vitte-editor-support
Installed-Size: $installed_size
Homepage: https://vitte-lang.org/
Vcs-Browser: https://github.com/vitte-lang/vitte
X-Vitte-Processor: $arch
Description: Complete Vitte systems language toolchain
 Compiler, runtime, standard library, sources, documentation, examples,
 shell completions and syntax highlighting for Vim, Neovim, Emacs,
 Nano and Geany.
EOF
}
write_maintainer_scripts() {
  control_root=$1
  cat > "$control_root/preinst" <<'EOF'
#!/bin/sh
set -eu
# Only remove broken, unowned legacy symlinks. dpkg handles package upgrades.
case "${1:-}" in
  install|upgrade)
    for path in /usr/local/bin/vitte /usr/local/bin/vittec /usr/bin/vitte /usr/bin/vittec; do
      if [ -L "$path" ] && [ ! -e "$path" ] && ! dpkg-query -S "$path" >/dev/null 2>&1; then
        rm -f -- "$path"
      fi
    done
    ;;
esac
exit 0
EOF
  chmod 0755 "$control_root/preinst"
  cat > "$control_root/postinst" <<'EOF'
#!/bin/sh
set -eu
ensure_executable() {
  command_path=$1
  if [ -f "$command_path" ]; then
    chmod 0755 "$command_path"
  fi
}
ensure_nano_include() {
  nanorc=$1
  syntax_file=$2
  [ -f "$syntax_file" ] || return 0
  include_line="include \\"$syntax_file\\""
  if [ -f "$nanorc" ]; then
    if ! grep -F "$include_line" "$nanorc" >/dev/null 2>&1; then
      printf '\n%s\n' "$include_line" >> "$nanorc"
    fi
  else
    mkdir -p "$(dirname "$nanorc")"
    printf '%s\n' "$include_line" > "$nanorc"
  fi
}
ensure_executable /usr/local/bin/vitte
ensure_executable /usr/local/bin/vittec
ensure_executable /usr/local/bin/vitte-installer-doctor
ensure_executable /usr/local/libexec/vitte/vitte
ensure_executable /usr/local/libexec/vitte/vittec
ensure_nano_include \
  /etc/nanorc \
  /usr/share/nano/vitte.nanorc
if command -v update-icon-caches >/dev/null 2>&1; then
  update-icon-caches /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database >/dev/null 2>&1 || true
fi
printf '%s\n' 'Vitte editor integrations installed:'
printf '%s\n' '  Vim/Neovim: *.vit, *.vitte, *.vitl'
printf '%s\n' '  Emacs:      *.vit, *.vitte, *.vitl'
printf '%s\n' '  Nano:       *.vit, *.vitte, *.vitl'
printf '%s\n' '  Geany:      Vitte filetype definition'
exit 0
EOF
  chmod 0755 "$control_root/postinst"
  cat > "$control_root/prerm" <<'EOF'
#!/bin/sh
set -eu
exit 0
EOF
  chmod 0755 "$control_root/prerm"
  cat > "$control_root/postrm" <<'EOF'
#!/bin/sh
set -eu
remove_nano_include() {
  nanorc=$1
  syntax_file=$2
  [ -f "$nanorc" ] || return 0
  include_line="include \\"$syntax_file\\""
  temporary_file="${nanorc}.vitte-tmp.$$"
  grep -Fv "$include_line" "$nanorc" > "$temporary_file" || true
  cat "$temporary_file" > "$nanorc"
  rm -f "$temporary_file"
}
case "${1:-}" in
  purge)
    remove_nano_include \
      /etc/nanorc \
      /usr/share/nano/vitte.nanorc
    ;;
esac
exit 0
EOF
  chmod 0755 "$control_root/postrm"
}
verify_debian_package() {
  package_file=$1
  control_directory=$(mktemp -d)
  extract_directory=$(mktemp -d)

  cleanup_deb_verify() {
    rm -rf "$control_directory" "$extract_directory"
  }
  trap cleanup_deb_verify EXIT HUP INT TERM

  dpkg-deb --info "$package_file" >/dev/null
  dpkg-deb --contents "$package_file" >/dev/null
  dpkg-deb --ctrl-tarfile "$package_file" >/dev/null
  dpkg-deb --fsys-tarfile "$package_file" >/dev/null
  dpkg-deb -e "$package_file" "$control_directory"
  dpkg-deb -x "$package_file" "$extract_directory"

  for member in control md5sums preinst postinst prerm postrm; do
    [ -s "$control_directory/$member" ] ||
      die "missing Debian $member file"
  done

  package_name=$(dpkg-deb --field "$package_file" Package)
  package_version=$(dpkg-deb --field "$package_file" Version)
  package_arch=$(dpkg-deb --field "$package_file" Architecture)
  installed_size=$(dpkg-deb --field "$package_file" Installed-Size)

  [ "$package_name" = "$PACKAGE_NAME" ] ||
    die "unexpected Debian package name: $package_name"
  [ "$package_version" = "$VERSION" ] ||
    die "unexpected Debian package version: $package_version"

  case "$installed_size" in
    '' | *[!0-9]* | 0)
      die "invalid Debian Installed-Size: ${installed_size:-missing}"
      ;;
  esac

  for command in vitte vittec; do
    [ -x "$extract_directory/usr/local/bin/$command" ] ||
      die "missing or non-executable $command command in Debian package"
    [ -x "$extract_directory/usr/local/libexec/vitte/$command" ] ||
      die "missing or non-executable $command payload in Debian package"
  done

  verify_directory_not_empty \
    "$extract_directory/usr/local/share/vitte/editors" \
    "archived editor integrations in Debian package"
  verify_directory_not_empty \
    "$extract_directory/usr/local/share/vitte/completions" \
    "shell completions in Debian package"

  scripts_build_require_file "$extract_directory/usr/share/vim/vimfiles/syntax/vitte.vim" "Vim Vitte syntax in Debian package"
  scripts_build_require_file "$extract_directory/usr/share/vim/vimfiles/ftdetect/vitte.vim" "Vim Vitte filetype detection in Debian package"
  scripts_build_require_file "$extract_directory/usr/share/vim/vimfiles/ftplugin/vitte.vim" "Vim Vitte filetype plugin in Debian package"
  scripts_build_require_file "$extract_directory/usr/share/emacs/site-lisp/vitte/vitte-mode.el" "Emacs Vitte mode in Debian package"
  scripts_build_require_file "$extract_directory/etc/emacs/site-start.d/50vitte.el" "Emacs Vitte automatic loader in Debian package"
  scripts_build_require_file "$extract_directory/usr/share/nano/vitte.nanorc" "Nano Vitte syntax in Debian package"
  scripts_build_require_file "$extract_directory/usr/share/geany/filedefs/filetypes.Vitte.conf" "Geany Vitte filetype in Debian package"
  scripts_build_require_file "$extract_directory/usr/local/share/vitte/assets/logo.png" "Vitte logo in Debian package"

  grep -F '*.vit' "$extract_directory/usr/share/vim/vimfiles/ftdetect/vitte.vim" >/dev/null ||
    die "Vim filetype detection does not register .vit"
  grep -E 'syntax[[:space:]]+"?[Vv]itte' "$extract_directory/usr/share/nano/vitte.nanorc" >/dev/null ||
    die "Nano syntax file does not declare Vitte syntax"

  printf '[build-linux-debs] verified Debian package: package=%s version=%s arch=%s installed-size=%s KiB\n' \
    "$package_name" "$package_version" "$package_arch" "$installed_size"

  cleanup_deb_verify
  trap - EXIT HUP INT TERM
}

build_one() {
  arch=$1
  stage=$ROOT_DIR/target/installer-linux-$arch
  package_root=$stage/package
  control_root=$package_root/DEBIAN
  data_root=$package_root
  package_file=$OUT_DIR/${PACKAGE_NAME}_${VERSION}_${arch}.deb
  checksum_file=$package_file.sha256

  printf '[build-linux-debs] building Linux %s package\n' "$arch"
  rm -rf "$stage"
  rm -f "$package_file" "$checksum_file" "$package_file.MANIFEST.json"
  mkdir -p "$control_root" "$OUT_DIR"

  VERSION=$VERSION \
  SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH \
    "$PAYLOAD_SCRIPT" "$data_root" linux "$arch" unix

  install_editor_integrations "$data_root"
  add_assets "$data_root"
  verify_payload "$data_root" "$arch"
  normalize_payload "$data_root"

  installed_size=$(du -sk "$data_root" | awk '{print $1}')
  case "$installed_size" in
    '' | *[!0-9]* | 0)
      die "invalid Installed-Size for Linux $arch: ${installed_size:-missing}"
      ;;
  esac

  write_control_file "$control_root" "$arch" "$installed_size"
  generate_md5sums "$data_root" "$control_root/md5sums"
  write_maintainer_scripts "$control_root"

  chmod 0755 "$control_root"
  chmod 0644 "$control_root/control" "$control_root/md5sums"
  chmod 0755 "$control_root/preinst" "$control_root/postinst" "$control_root/prerm" "$control_root/postrm"
  normalize_payload "$package_root"

  # dpkg-deb creates Debian-compatible tar members without bsdtar PAX headers.
  DPKG_DEB_COMPRESSOR_TYPE=gzip \
  DPKG_DEB_COMPRESSOR_LEVEL=9 \
    dpkg-deb --root-owner-group --build "$package_root" "$package_file"

  scripts_build_require_file "$package_file" "Debian package"
  verify_debian_package "$package_file"

  scripts_build_sha256_write "$package_file" "$checksum_file"
  scripts_build_sha256_check "$package_file" "$checksum_file"
  scripts_build_write_artifact_manifest "$package_file" linux "$arch" "$VERSION"

  package_size=$(wc -c < "$package_file" | tr -d ' ')
  [ "$package_size" -gt 1024 ] ||
    die "Debian package is too small to contain the Vitte toolchain: $package_file ($package_size bytes)"

  printf '[build-linux-debs] wrote %s (%s bytes)\n' "$package_file" "$package_size"
  printf '[build-linux-debs] wrote %s\n' "$checksum_file"
  printf '[build-linux-debs] wrote %s\n' "$package_file.MANIFEST.json"
}

normalize_arch() {
  case "$1" in
    x86_64 | X86_64 | amd64 | AMD64)
      printf '%s\n' amd64
      ;;
    aarch64 | AArch64 | AARCH64 | arm64 | ARM64)
      printf '%s\n' arm64
      ;;
    armhf | armv7 | armv7l)
      printf '%s\n' armhf
      ;;
    armel | armv6)
      printf '%s\n' armel
      ;;
    i386 | i486 | i586 | i686 | x86)
      printf '%s\n' i386
      ;;
    riscv64 | RISC-V64 | RISCV64)
      printf '%s\n' riscv64
      ;;
    ppc64el | powerpc64le | ppc64le)
      printf '%s\n' ppc64el
      ;;
    s390x)
      printf '%s\n' s390x
      ;;
    mips64el)
      printf '%s\n' mips64el
      ;;
    mipsel)
      printf '%s\n' mipsel
      ;;
    powerpc | ppc)
      printf '%s\n' powerpc
      ;;
    sparc64)
      printf '%s\n' sparc64
      ;;
    *)
      die "unsupported Linux architecture: $1"
      ;;
  esac
}
supported_arches='amd64 arm64 armhf armel i386 riscv64 ppc64el s390x mips64el mipsel powerpc sparc64'
if [ "$LIST_TARGETS" -eq 1 ]; then
  for arch in $supported_arches; do
    printf 'linux %s\n' "$arch"
  done
  exit 0
fi
[ -x "$PAYLOAD_SCRIPT" ] ||
  die "payload staging script is missing or not executable: $PAYLOAD_SCRIPT"
require_directory \
  "$EDITORS_DIR" \
  "editor integrations"
require_directory \
  "$COMPLETIONS_DIR" \
  "shell completions"
scripts_build_require_file \
  "$LOGO_FILE" \
  "Vitte logo"
for tool in \
  awk \
  cat \
  date \
  dpkg-deb \
  du \
  find \
  grep \
  install \
  mktemp \
  python3 \
  sed \
  touch \
  wc
do
  scripts_build_require "$tool"
done
case "$ARCH" in
  all)
    for arch in $supported_arches; do
      build_one "$arch"
    done
    ;;
  *)
    normalized_arch=$(normalize_arch "$ARCH")
    build_one "$normalized_arch"
    ;;
esac
printf \
  '[build-linux-debs] complete version=%s arch=%s out=%s\n' \
  "$VERSION" \
  "$ARCH" \
  "$OUT_DIR"
