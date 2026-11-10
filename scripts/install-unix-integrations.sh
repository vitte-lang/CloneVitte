#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
SHARE_ROOT=$DESTDIR$PREFIX/share/vitte
EDITOR_ROOT=$SHARE_ROOT/editors
COMPLETION_ROOT=$SHARE_ROOT/completions
MODE=${1:-install}

case "$PREFIX" in
  /*) ;;
  *) printf 'PREFIX must be absolute: %s\n' "$PREFIX" >&2; exit 2 ;;
esac

die() {
  printf '[vitte-integrations][error] %s\n' "$*" >&2
  exit 1
}

install_file() {
  source=$1
  destination=$2
  [ -s "$source" ] || die "integration source missing: $source"
  mkdir -p "$(dirname "$destination")"
  install -m 0644 "$source" "$destination"
}

remove_file() {
  rm -f "$1"
}

remove_nano_include() {
  nanorc=$1
  include_line="include \"$PREFIX/share/nano/vitte.nanorc\""
  [ -f "$nanorc" ] || return 0
  temporary=$nanorc.vitte-tmp.$$
  grep -Fv "$include_line" "$nanorc" > "$temporary" || true
  cat "$temporary" > "$nanorc"
  rm -f "$temporary"
}

if [ "$MODE" = uninstall ]; then
  for path in \
    "$DESTDIR$PREFIX/share/vim/vimfiles/syntax/vitte.vim" \
    "$DESTDIR$PREFIX/share/vim/vimfiles/ftdetect/vitte.vim" \
    "$DESTDIR$PREFIX/share/vim/vimfiles/ftplugin/vitte.vim" \
    "$DESTDIR$PREFIX/share/emacs/site-lisp/vitte-mode.el" \
    "$DESTDIR$PREFIX/share/emacs/site-lisp/site-start.d/vitte-init.el" \
    "$DESTDIR$PREFIX/share/nano/vitte.nanorc" \
    "$DESTDIR$PREFIX/share/geany/filedefs/filetypes.Vitte.conf" \
    "$DESTDIR$PREFIX/share/geany/filedefs/snippets.vitte.conf" \
    "$DESTDIR$PREFIX/share/bash-completion/completions/vitte" \
    "$DESTDIR$PREFIX/share/fish/vendor_completions.d/vitte.fish" \
    "$DESTDIR$PREFIX/share/zsh/site-functions/_vitte"
  do
    remove_file "$path"
  done
  for relative in indent/vitte.vim compiler/vitte.vim autoload/vitte.vim autoload/vitte/complete.vim after/syntax/vitte.vim colors/vitte_reference.vim ultisnips/vitte.snippets; do
    remove_file "$DESTDIR$PREFIX/share/vim/vimfiles/$relative"
  done
  remove_file "$DESTDIR$PREFIX/share/emacs/site-lisp/vitte-indent.el"
  remove_file "$DESTDIR$PREFIX/bin/vitte-editor"
  remove_file "$DESTDIR$PREFIX/bin/vitte-lsp"
  remove_nano_include "$DESTDIR/etc/nanorc"
  remove_nano_include "$DESTDIR$PREFIX/etc/nanorc"
  if [ -z "$DESTDIR" ]; then
    remove_nano_include /etc/nanorc
  fi
  printf '%s\n' '[vitte-integrations] removed Vitte editor and shell support'
  exit 0
fi

[ "$MODE" = install ] || die "usage: $0 [install|uninstall]"

install_file "$EDITOR_ROOT/vim/syntax/vitte.vim" \
  "$DESTDIR$PREFIX/share/vim/vimfiles/syntax/vitte.vim"
install_file "$EDITOR_ROOT/vim/ftdetect/vitte.vim" \
  "$DESTDIR$PREFIX/share/vim/vimfiles/ftdetect/vitte.vim"
install_file "$EDITOR_ROOT/vim/ftplugin/vitte.vim" \
  "$DESTDIR$PREFIX/share/vim/vimfiles/ftplugin/vitte.vim"

for relative in indent/vitte.vim compiler/vitte.vim autoload/vitte.vim autoload/vitte/complete.vim after/syntax/vitte.vim colors/vitte_reference.vim ultisnips/vitte.snippets; do
  install_file "$EDITOR_ROOT/vim/$relative" "$DESTDIR$PREFIX/share/vim/vimfiles/$relative"
done
install_file "$EDITOR_ROOT/emacs/vitte-indent.el" \
  "$DESTDIR$PREFIX/share/emacs/site-lisp/vitte-indent.el"

install_file "$EDITOR_ROOT/emacs/vitte-mode.el" \
  "$DESTDIR$PREFIX/share/emacs/site-lisp/vitte-mode.el"
emacs_startup=$DESTDIR$PREFIX/share/emacs/site-lisp/site-start.d/vitte-init.el
mkdir -p "$(dirname "$emacs_startup")"
cat > "$emacs_startup" <<EOF
;;; vitte-init.el --- system-wide Vitte mode registration
(add-to-list 'load-path "$PREFIX/share/emacs/site-lisp")
(autoload 'vitte-mode "vitte-mode" "Major mode for the Vitte programming language." t)
(add-to-list 'auto-mode-alist '("\\.vit\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\.vitte\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\.vitl\\'" . vitte-mode))
(provide 'vitte-init)
;;; vitte-init.el ends here
EOF
chmod 0644 "$emacs_startup"

install_file "$EDITOR_ROOT/nano/vitte.nanorc" \
  "$DESTDIR$PREFIX/share/nano/vitte.nanorc"
mkdir -p "$DESTDIR$PREFIX/bin" "$DESTDIR$PREFIX/share/geany/filedefs"
cat > "$DESTDIR$PREFIX/bin/vitte-editor" <<'WRAPPER'
#!/bin/sh
set -eu
payload=$(CDPATH= cd -- "$(dirname -- "$0")/../share/vitte" && pwd)
exec "${PYTHON:-python3}" "$payload/editors/vitte-editor" "$@"
WRAPPER
cat > "$DESTDIR$PREFIX/bin/vitte-lsp" <<'WRAPPER'
#!/bin/sh
exec "$(dirname -- "$0")/vitte-editor" lsp "$@"
WRAPPER
chmod 0755 "$DESTDIR$PREFIX/bin/vitte-editor" "$DESTDIR$PREFIX/bin/vitte-lsp"
python3 "$EDITOR_ROOT/geany/integration.py" --render-filetype --helper "$PREFIX/bin/vitte-editor" \
  > "$DESTDIR$PREFIX/share/geany/filedefs/filetypes.Vitte.conf"
install_file "$EDITOR_ROOT/geany/snippets.vitte.conf" \
  "$DESTDIR$PREFIX/share/geany/filedefs/snippets.vitte.conf"

install_file "$COMPLETION_ROOT/bash/vitte" \
  "$DESTDIR$PREFIX/share/bash-completion/completions/vitte"
install_file "$COMPLETION_ROOT/fish/vitte.fish" \
  "$DESTDIR$PREFIX/share/fish/vendor_completions.d/vitte.fish"
install_file "$COMPLETION_ROOT/zsh/_vitte" \
  "$DESTDIR$PREFIX/share/zsh/site-functions/_vitte"

append_nano_include() {
  nanorc=$1
  include_line="include \"$PREFIX/share/nano/vitte.nanorc\""
  [ -f "$nanorc" ] || return 0
  grep -F "$include_line" "$nanorc" >/dev/null 2>&1 && return 0
  printf '\n%s\n' "$include_line" >> "$nanorc"
}

append_nano_include "$DESTDIR/etc/nanorc"
append_nano_include "$DESTDIR$PREFIX/etc/nanorc"
if [ -z "$DESTDIR" ]; then
  append_nano_include /etc/nanorc
fi

printf '%s\n' '[vitte-integrations] installed Vim, Emacs, Nano, Geany, Bash, Fish and Zsh support'
