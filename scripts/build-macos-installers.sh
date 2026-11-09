#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-macos-installers
. "$ROOT_DIR/scripts/common.sh"
scripts_build_parse_common_flags "$@"
VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}
case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac
ARCH=${ARCH:-all}
STRICT_DMG=${STRICT_DMG:-0}
DMG_PRESENTATION=${DMG_PRESENTATION:-1}
SIGN=${SIGN:-0}
NOTARIZE=${NOTARIZE:-0}
MACOS_SIGN_IDENTITY=${MACOS_SIGN_IDENTITY:-}
MACOS_NOTARY_PROFILE=${MACOS_NOTARY_PROFILE:-}

EDITORS_DIR=$ROOT_DIR/editors
COMPLETIONS_DIR=$ROOT_DIR/completions
LOGO_FILE=$ROOT_DIR/assets/logo.png

MACOS2006_BINARY=${MACOS2006_BINARY:-$ROOT_DIR/target/macos2006-i386/vitte}
MACOS2006_DEPLOYMENT_TARGET=${MACOS2006_DEPLOYMENT_TARGET:-10.4}
MACOS2006_SDK=${MACOS2006_SDK:-MacOSX10.4u.sdk}
MACOS2006_CC=${MACOS2006_CC:-gcc-4.0}

scripts_build_maybe_help "usage: build-macos-installers.sh [--dry-run]"
scripts_build_maybe_dry_run "would build macOS pkg artifacts version=$VERSION arch=$ARCH strict_dmg=$STRICT_DMG out=$OUT_DIR"

die() {
  printf '[build-macos-installers][error] %s\n' "$*" >&2
  exit 1
}

log() {
  printf '[build-macos-installers] %s\n' "$*"
}

case "$DMG_PRESENTATION" in
  0 | 1) ;;
  *) die "DMG_PRESENTATION must be 0 or 1" ;;
esac

require() {
  command -v "$1" >/dev/null 2>&1 ||
    die "missing required tool: $1"
}

require_file() {
  file=$1
  description=$2

  [ -f "$file" ] ||
    die "$description not found: $file"

  [ -s "$file" ] ||
    die "$description is empty: $file"
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


check_arch() {
  binary=$1
  expected=$2

  require_file "$binary" "Vitte compiler binary"

  lipo -archs "$binary" |
    tr ' ' '\n' |
    grep -Fx "$expected" >/dev/null ||
    die "$binary does not contain required architecture $expected"
}

copy_tree() {
  source_dir=$1
  destination_dir=$2

  [ -d "$source_dir" ] ||
    die "source directory not found: $source_dir"

  scripts_build_copy_tree "$source_dir" "$destination_dir"
}

copy_resources() {
  destination=$1

  mkdir -p "$destination"

  install -m 0644 \
    "$LOGO_FILE" \
    "$destination/logo.png"
}

write_checksum() {
  file=$1

  require_file "$file" "checksum input"

  (
    cd "$OUT_DIR"
    filename=$(basename "$file")
    scripts_build_sha256_write "$OUT_DIR/$filename" "$OUT_DIR/$filename.sha256"
  )

  log "wrote $file.sha256"
}

create_vim_integration() {
  payload_root=$1

  vim_syntax=$(find_first_file \
    "Vim syntax definition" \
    "$EDITORS_DIR/vim/syntax/vitte.vim" \
    "$EDITORS_DIR/vim/vitte.vim" \
    "$EDITORS_DIR/vitte.vim")

  vim_root=$payload_root/usr/local/share/vim/vimfiles

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
    chmod 0644 "$vim_root/ftdetect/vitte.vim"
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
    chmod 0644 "$vim_root/ftplugin/vitte.vim"
  fi

  log "staged Vim syntax integration"
}

create_emacs_integration() {
  payload_root=$1

  emacs_mode=$(find_first_file \
    "Emacs Vitte mode" \
    "$EDITORS_DIR/emacs/vitte-mode.el" \
    "$EDITORS_DIR/emacs/vitte.el" \
    "$EDITORS_DIR/vitte-mode.el")

  emacs_root=$payload_root/usr/local/share/emacs/site-lisp

  mkdir -p \
    "$emacs_root" \
    "$emacs_root/site-start.d"

  install -m 0644 \
    "$emacs_mode" \
    "$emacs_root/vitte-mode.el"

  cat > "$emacs_root/site-start.d/vitte-init.el" <<'EOF'
;;; vitte-init.el --- system-wide Vitte mode registration

(add-to-list 'load-path "/usr/local/share/emacs/site-lisp")

(autoload 'vitte-mode
  "vitte-mode"
  "Major mode for the Vitte programming language."
  t)

(add-to-list 'auto-mode-alist '("\\.vit\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\.vitte\\'" . vitte-mode))
(add-to-list 'auto-mode-alist '("\\.vitl\\'" . vitte-mode))

(provide 'vitte-init)

;;; vitte-init.el ends here
EOF

  chmod 0644 "$emacs_root/site-start.d/vitte-init.el"

  log "staged Emacs mode integration"
}

create_nano_integration() {
  payload_root=$1

  nano_syntax=$(find_first_file \
    "Nano Vitte syntax" \
    "$EDITORS_DIR/nano/vitte.nanorc" \
    "$EDITORS_DIR/nano/vitte.nano" \
    "$EDITORS_DIR/vitte.nanorc")

  nano_root=$payload_root/usr/local/share/nano

  mkdir -p "$nano_root"

  install -m 0644 \
    "$nano_syntax" \
    "$nano_root/vitte.nanorc"

  log "staged Nano syntax integration"
}

create_geany_integration() {
  payload_root=$1

  geany_definition=$(find_first_file \
    "Geany Vitte filetype" \
    "$EDITORS_DIR/geany/filetypes.Vitte.conf" \
    "$EDITORS_DIR/geany/filetypes.vitte.conf" \
    "$EDITORS_DIR/geany/vitte.conf" \
    "$EDITORS_DIR/filetypes.Vitte.conf")

  geany_unix_root=$payload_root/usr/local/share/geany/filedefs
  geany_macos_root="$payload_root/Library/Application Support/geany/filedefs"

  mkdir -p \
    "$geany_unix_root" \
    "$geany_macos_root"

  install -m 0644 \
    "$geany_definition" \
    "$geany_unix_root/filetypes.Vitte.conf"

  install -m 0644 \
    "$geany_definition" \
    "$geany_unix_root/filetypes.vitte.conf"

  install -m 0644 \
    "$geany_definition" \
    "$geany_macos_root/filetypes.Vitte.conf"

  install -m 0644 \
    "$geany_definition" \
    "$geany_macos_root/filetypes.vitte.conf"

  log "staged Geany filetype integration"
}

create_editor_payload() {
  payload_root=$1

  archive_root=$payload_root/usr/local/share/vitte/editors
  completion_root=$payload_root/usr/local/share/vitte/completions

  rm -rf "$payload_root"

  mkdir -p \
    "$payload_root" \
    "$archive_root" \
    "$completion_root"

  copy_tree "$EDITORS_DIR" "$archive_root"
  copy_tree "$COMPLETIONS_DIR" "$completion_root"

  for integration_prefix in /usr/local /opt/homebrew; do
    bash_destination=$payload_root$integration_prefix/etc/bash_completion.d/vitte
    fish_destination=$payload_root$integration_prefix/share/fish/vendor_completions.d/vitte.fish
    zsh_destination=$payload_root$integration_prefix/share/zsh/site-functions/_vitte
    mkdir -p \
      "$(dirname "$bash_destination")" \
      "$(dirname "$fish_destination")" \
      "$(dirname "$zsh_destination")"
    install -m 0644 "$COMPLETIONS_DIR/bash/vitte" "$bash_destination"
    install -m 0644 "$COMPLETIONS_DIR/fish/vitte.fish" "$fish_destination"
    install -m 0644 "$COMPLETIONS_DIR/zsh/_vitte" "$zsh_destination"
  done

  create_vim_integration "$payload_root"
  create_emacs_integration "$payload_root"
  create_nano_integration "$payload_root"
  create_geany_integration "$payload_root"
}

write_editor_postinstall() {
  scripts_root=$1

  mkdir -p "$scripts_root"

  cat > "$scripts_root/preinstall" <<'EOF'
#!/bin/sh
set -eu

LOG_FILE=/var/log/vitte-install.log
if ! touch "$LOG_FILE" >/dev/null 2>&1; then
  LOG_FILE=/tmp/vitte-install.log
  touch "$LOG_FILE" >/dev/null 2>&1 || true
fi

log_line() {
  printf "%s %s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >> "$LOG_FILE" 2>/dev/null || true
}

prepare_dir() {
  path=$1
  if [ -e "$path" ] && [ ! -d "$path" ]; then
    rm -f "$path"
    log_line "[editors preinstall] removed blocking file: $path"
  fi
  mkdir -p "$path"
}

prepare_dir /usr/local/share
prepare_dir /usr/local/share/vim
prepare_dir /usr/local/share/vim/vimfiles
prepare_dir /usr/local/share/emacs
prepare_dir /usr/local/share/emacs/site-lisp
prepare_dir /usr/local/share/nano
prepare_dir /usr/local/share/geany
prepare_dir /usr/local/share/geany/filedefs
prepare_dir "/Library/Application Support"
prepare_dir "/Library/Application Support/geany"
prepare_dir "/Library/Application Support/geany/filedefs"

log_line "[editors preinstall] ready"
exit 0
EOF

  cat > "$scripts_root/postinstall" <<'EOF'
#!/bin/sh
set -eu

LOG_FILE=/var/log/vitte-install.log
if ! touch "$LOG_FILE" >/dev/null 2>&1; then
  LOG_FILE=/tmp/vitte-install.log
  touch "$LOG_FILE" >/dev/null 2>&1 || true
fi

NANO_SYNTAX=/usr/local/share/nano/vitte.nanorc
NANO_INCLUDE='include "/usr/local/share/nano/vitte.nanorc"'

log_line() {
  printf "%s %s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >> "$LOG_FILE" 2>/dev/null || true
}

append_nano_include() {
  nanorc=$1

  if [ -f "$nanorc" ]; then
    if ! grep -F "$NANO_INCLUDE" "$nanorc" >/dev/null 2>&1; then
      printf '\n%s\n' "$NANO_INCLUDE" >> "$nanorc" ||
        log_line "[editors postinstall] could not update $nanorc"
    fi
  else
    mkdir -p "$(dirname "$nanorc")" 2>/dev/null || {
      log_line "[editors postinstall] could not create $(dirname "$nanorc")"
      return 0
    }
    printf '%s\n' "$NANO_INCLUDE" > "$nanorc" ||
      log_line "[editors postinstall] could not write $nanorc"
  fi
}

install_geany_for_user() {
  user_home=$1
  user_name=$2
  source_root=$3
  destination_root=$4

  [ -d "$user_home" ] || return 0
  [ -f "$source_root/filetypes.Vitte.conf" ] || return 0

  mkdir -p "$destination_root" 2>/dev/null || {
    log_line "[editors postinstall] could not create $destination_root"
    return 0
  }

  cp \
    "$source_root/filetypes.Vitte.conf" \
    "$destination_root/filetypes.Vitte.conf" ||
    log_line "[editors postinstall] could not install Geany filetype for $user_name"

  cp \
    "$source_root/filetypes.vitte.conf" \
    "$destination_root/filetypes.vitte.conf" ||
    log_line "[editors postinstall] could not install lowercase Geany filetype for $user_name"

  chown -R "$user_name" "$destination_root" 2>/dev/null || true
}

if [ -f "$NANO_SYNTAX" ]; then
  append_nano_include /etc/nanorc

  if [ -d /usr/local/etc ]; then
    append_nano_include /usr/local/etc/nanorc
  fi

  if [ -d /opt/homebrew/etc ]; then
    append_nano_include /opt/homebrew/etc/nanorc
  fi
else
  log_line "[editors postinstall] nano syntax not found at $NANO_SYNTAX"
fi

for user_home in /Users/*; do
  [ -d "$user_home" ] || continue

  user_name=$(basename "$user_home")

  case "$user_name" in
    Shared)
      continue
      ;;
  esac

  install_geany_for_user \
    "$user_home" \
    "$user_name" \
    "/Library/Application Support/geany/filedefs" \
    "$user_home/.config/geany/filedefs"

  install_geany_for_user \
    "$user_home" \
    "$user_name" \
    "/Library/Application Support/geany/filedefs" \
    "$user_home/Library/Application Support/geany/filedefs"
done

log_line "[editors postinstall] done"
exit 0
EOF

  chmod 0755 "$scripts_root/preinstall"
  chmod 0755 "$scripts_root/postinstall"
}

build_editor_component() {
  component_dir=$1
  component_pkg=$2

  payload_root=$component_dir/editor-payload
  scripts_root=$component_dir/editor-scripts

  rm -rf \
    "$payload_root" \
    "$scripts_root" \
    "$component_pkg"

  create_editor_payload "$payload_root"
  write_editor_postinstall "$scripts_root"

  mkdir -p "$(dirname "$component_pkg")"

  pkgbuild \
    --root "$payload_root" \
    --identifier org.vitte.editors \
    --version "$VERSION" \
    --install-location / \
    --scripts "$scripts_root" \
    "$component_pkg"

  require_file \
    "$component_pkg" \
    "Vitte editor integration component package"
}

write_toolchain_scripts() {
  scripts_root=$1

  mkdir -p "$scripts_root"

  cat > "$scripts_root/preinstall" <<'EOF'
#!/bin/sh
set -eu
mkdir -p /usr/local/bin /usr/local/libexec /usr/local/share
exit 0
EOF

  cat > "$scripts_root/postinstall" <<'EOF'
#!/bin/sh
set -eu
chmod 0755 /usr/local/bin/vitte /usr/local/bin/vittec 2>/dev/null || true
chmod 0755 /usr/local/libexec/vitte/vitte /usr/local/libexec/vitte/vittec 2>/dev/null || true
exit 0
EOF

  chmod 0755 "$scripts_root/preinstall" "$scripts_root/postinstall"
}

build_toolchain_component() {
  component_dir=$1
  component_pkg=$2
  label=$3
  binary=$4

  payload_root=$component_dir/toolchain-payload
  scripts_root=$component_dir/toolchain-scripts
  command_dir=$component_dir/toolchain-command

  rm -rf "$payload_root" "$scripts_root" "$component_pkg" "$command_dir"
  mkdir -p "$command_dir" "$(dirname "$component_pkg")"

  install -m 0755 "$binary" "$command_dir/vitte"
  install -m 0755 "$binary" "$command_dir/vittec"

  VERSION=$VERSION \
    VITTE_BIN=$binary \
    "$ROOT_DIR/scripts/stage-installer-payload.sh" \
    "$payload_root" \
    macos \
    "$label" \
    unix

  write_toolchain_scripts "$scripts_root"

  pkgbuild \
    --root "$payload_root" \
    --identifier org.vitte.toolchain \
    --version "$VERSION" \
    --install-location / \
    --scripts "$scripts_root" \
    "$component_pkg"

  require_file \
    "$component_pkg" \
    "Vitte toolchain component package"
}

create_dmg() {
  package_file=$1
  dmg_file=$2
  volume_name=$3
  architecture=$4
  minimum_system=$5

  stage=$ROOT_DIR/target/macos-dmg-stage/$volume_name
  rw_dmg=$ROOT_DIR/target/macos-dmg-stage/$volume_name-rw.dmg
  mount_path=$ROOT_DIR/target/macos-dmg-stage/$volume_name-mount

  [ ! -d "$stage" ] || chmod -R u+w "$stage" 2>/dev/null || true
  rm -rf "$stage"
  rm -f "$dmg_file"
  rm -f "$rw_dmg"
  rm -rf "$mount_path"

  mkdir -p "$stage/.background"

  cp "$package_file" "$stage/Install Vitte.pkg"
  ln -s /Applications "$stage/Applications"
  install -m 0644 "$LOGO_FILE" "$stage/Vitte-logo.png"

  # Finder stores the visual treatment in the image itself. A dark, quiet
  # background keeps the installer package and the Applications shortcut
  # readable while the logo provides the Vitte accent.
  install -m 0644 "$LOGO_FILE" "$stage/.background/Vitte-logo.png"

  cat > "$stage/INSTALL.txt" <<EOF
Vitte $VERSION for macOS

Architecture: $architecture
Minimum target system: $minimum_system

Open "Install Vitte.pkg" to install Vitte.

Drag the package toward Applications if you want the standard macOS gesture;
the package still installs the compiler into /usr/local when opened.

Installed editor integrations:

Vim:
    /usr/local/share/vim/vimfiles/syntax/vitte.vim
    /usr/local/share/vim/vimfiles/ftdetect/vitte.vim
    /usr/local/share/vim/vimfiles/ftplugin/vitte.vim

Emacs:
    /usr/local/share/emacs/site-lisp/vitte-mode.el
    /usr/local/share/emacs/site-lisp/site-start.d/vitte-init.el

Nano:
    /usr/local/share/nano/vitte.nanorc

Geany:
    /usr/local/share/geany/filedefs/filetypes.Vitte.conf
    /Library/Application Support/geany/filedefs/filetypes.Vitte.conf
    archived lowercase source: /usr/local/share/vitte/editors/geany/filetypes.vitte.conf

Supported Vitte extensions:

    .vit
    .vitte
    .vitl
EOF

  if [ "$DMG_PRESENTATION" -eq 1 ] && command -v osascript >/dev/null 2>&1; then
    hdiutil create \
      -ov \
      -format UDRW \
      -fs HFS+ \
      -volname "$volume_name" \
      -srcfolder "$stage" \
      "$rw_dmg" >/dev/null || {
        rm -f "$rw_dmg"
        if [ "$STRICT_DMG" -eq 1 ]; then
          die "hdiutil failed while creating the writable macOS DMG: $rw_dmg"
        fi
        log "DMG presentation deferred: writable image could not be created"
        return 0
      }

    mkdir -p "$mount_path"
    hdiutil attach "$rw_dmg" \
      -nobrowse \
      -noautoopen \
      -mountpoint "$mount_path" >/dev/null || {
      rm -f "$rw_dmg"
      rm -rf "$mount_path"
      if [ "$STRICT_DMG" -eq 1 ]; then
        die "hdiutil failed while mounting the writable macOS DMG: $rw_dmg"
      fi
      log "DMG presentation deferred: writable image could not be mounted"
      return 0
    }

    if ! osascript >/dev/null <<EOF
tell application "Finder"
  open (POSIX file "$mount_path" as alias)
  delay 1
  set dmgWindow to front window
  set current view of dmgWindow to icon view
  set toolbar visible of dmgWindow to false
  set statusbar visible of dmgWindow to false
  set bounds of dmgWindow to {120, 120, 920, 650}
  set viewOptions to the icon view options of dmgWindow
  set icon size of viewOptions to 128
  set text size of viewOptions to 13
  set arrangement of viewOptions to not arranged
  set background color of viewOptions to {9000, 11000, 16000}
  set position of item "Install Vitte.pkg" of dmgWindow to {245, 275}
  set position of item "Applications" of dmgWindow to {605, 275}
  set position of item "Vitte-logo.png" of dmgWindow to {425, 505}
  close dmgWindow
end tell
EOF
    then
      log "Finder presentation could not be applied; keeping the DMG contents intact"
    fi

    sync
    hdiutil detach "$mount_path" -quiet || {
      hdiutil detach "$mount_path" -force -quiet || true
    }
    rm -rf "$mount_path"

    hdiutil convert "$rw_dmg" \
      -ov \
      -format UDZO \
      -imagekey zlib-level=9 \
      -o "$dmg_file" >/dev/null
    rm -f "$rw_dmg"
  elif ! hdiutil create \
      -ov \
      -format UDZO \
      -fs HFS+ \
      -volname "$volume_name" \
      -srcfolder "$stage" \
      "$dmg_file"
  then
    if [ "$STRICT_DMG" -eq 1 ]; then
      die "hdiutil failed while creating macOS DMG: $dmg_file"
    fi
    rm -f "$dmg_file"
    log "DMG deferred: hdiutil could not create $dmg_file on this host"
    return 0
  fi

  require_file "$dmg_file" "macOS DMG"
}

verify_product_package() {
  package_file=$1
  label=$2

  expanded=$ROOT_DIR/target/macos-pkg-verification-$label

  [ ! -d "$expanded" ] || chmod -R u+w "$expanded" 2>/dev/null || true
  rm -rf "$expanded"

  pkgutil --expand-full "$package_file" "$expanded"

  test -d "$expanded/vitte-component.pkg" ||
    die "missing macOS toolchain component package"

  test -d "$expanded/vitte-editors.pkg" ||
    die "missing macOS editor component package"

  test -s "$expanded/Distribution" ||
    die "missing macOS Distribution resource"

  test -s "$expanded/Resources/Welcome.txt" ||
    die "missing macOS welcome resource"

  test -s "$expanded/Resources/License.txt" ||
    die "missing macOS license resource"

  test -s "$expanded/Resources/Conclusion.txt" ||
    die "missing macOS conclusion resource"

  test -x "$expanded/vitte-editors.pkg/Scripts/postinstall" ||
    die "missing macOS editor postinstall script"

  test -x "$expanded/vitte-editors.pkg/Scripts/preinstall" ||
    die "missing macOS editor preinstall script"

  test -x "$expanded/vitte-component.pkg/Scripts/preinstall" ||
    die "missing macOS toolchain preinstall script"

  test -x "$expanded/vitte-component.pkg/Scripts/postinstall" ||
    die "missing macOS toolchain postinstall script"

  find "$expanded" \
    -path '*/Payload/usr/local/bin/vitte' \
    -type f \
    -perm -111 \
    -print \
    -quit |
    grep -q . ||
    die "missing Vitte command in macOS $label package"

  packaged_binary=$(find "$expanded" \
    -path '*/Payload/usr/local/libexec/vitte/vitte' \
    -type f \
    -perm -111 \
    -print \
    -quit)
  [ -n "$packaged_binary" ] ||
    die "missing packaged Vitte payload in macOS $label package"

  packaged_arches=$(lipo -archs "$packaged_binary")
  case "$label" in
    universal | universal2)
      printf '%s\n' "$packaged_arches" |
        tr ' ' '\n' |
        grep -Fx arm64 >/dev/null ||
        die "packaged Vitte payload is missing arm64 for macOS $label"
      printf '%s\n' "$packaged_arches" |
        tr ' ' '\n' |
        grep -Fx x86_64 >/dev/null ||
        die "packaged Vitte payload is missing x86_64 for macOS $label"
      ;;
    *)
      printf '%s\n' "$packaged_arches" |
        tr ' ' '\n' |
        grep -Fx "$label" >/dev/null ||
        die "packaged Vitte payload has the wrong architecture for macOS $label"
      ;;
  esac

  module_package=$(find "$expanded" \
    -path '*/Payload/usr/local/share/vitte/modules/*/package.toml' \
    -type f \
    -print \
    -quit)
  [ -n "$module_package" ] ||
    die "missing JSON module in macOS $label package"

  installation_manifest=$(find "$expanded" \
    -path '*/Payload/usr/local/share/vitte/INSTALLATION.json' \
    -type f \
    -print \
    -quit)
  [ -n "$installation_manifest" ] ||
    die "missing installation manifest in macOS $label package"

  scripts_build_verify_modules \
    "$(dirname "$(dirname "$(dirname "$module_package")")")"

  for integration_path in \
    '*/Payload/usr/local/etc/bash_completion.d/vitte' \
    '*/Payload/usr/local/share/fish/vendor_completions.d/vitte.fish' \
    '*/Payload/usr/local/share/zsh/site-functions/_vitte' \
    '*/Payload/opt/homebrew/etc/bash_completion.d/vitte' \
    '*/Payload/opt/homebrew/share/fish/vendor_completions.d/vitte.fish' \
    '*/Payload/opt/homebrew/share/zsh/site-functions/_vitte'
  do
    find "$expanded" \
      -path "$integration_path" \
      -type f \
      -print \
      -quit |
      grep -q . ||
      die "missing shell completion in macOS package: $integration_path"
  done

  find "$expanded" \
    -path '*/Payload/usr/local/share/vim/vimfiles/syntax/vitte.vim' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Vim Vitte syntax in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/vim/vimfiles/ftdetect/vitte.vim' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Vim Vitte filetype detection in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/emacs/site-lisp/vitte-mode.el' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Emacs Vitte mode in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/emacs/site-lisp/site-start.d/vitte-init.el' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Emacs Vitte automatic loader in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/nano/vitte.nanorc' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Nano Vitte syntax in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/geany/filedefs/filetypes.Vitte.conf' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Geany Vitte filetype in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/vitte/editors/geany/filetypes.vitte.conf' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing archived lowercase Geany Vitte filetype in macOS package"

  find "$expanded" \
    -path '*/Payload/Library/Application Support/geany/filedefs/filetypes.Vitte.conf' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing macOS Geany Vitte filetype in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/vitte/editors/geany/snippets.vitte.conf' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing archived Geany snippets in macOS package"

  find "$expanded" \
    -path '*/Payload/usr/local/share/vitte/assets/logo.png' \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "missing Vitte logo in macOS package"

  chmod -R u+w "$expanded" 2>/dev/null || true
  rm -rf "$expanded"

  log "verified package payload: $package_file"
}

sign_macos_artifact() {
  artifact=$1
  [ "$SIGN" -eq 1 ] || return 0
  [ -n "$MACOS_SIGN_IDENTITY" ] ||
    die "SIGN=1 requires MACOS_SIGN_IDENTITY"
  scripts_build_require productsign
  signed=$artifact.signed
  productsign --sign "$MACOS_SIGN_IDENTITY" "$artifact" "$signed"
  mv "$signed" "$artifact"
}

notarize_macos_artifact() {
  artifact=$1
  [ "$NOTARIZE" -eq 1 ] || return 0
  [ "$SIGN" -eq 1 ] ||
    die "NOTARIZE=1 requires SIGN=1"
  [ -n "$MACOS_NOTARY_PROFILE" ] ||
    die "NOTARIZE=1 requires MACOS_NOTARY_PROFILE"
  scripts_build_require xcrun
  xcrun notarytool submit "$artifact" --keychain-profile "$MACOS_NOTARY_PROFILE" --wait
}

write_distribution() {
  distribution=$1
  expected_archs=$2
  minimum_system=$3

  cat > "$distribution" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Vitte $VERSION</title>

  <background
    file="logo.png"
    alignment="center"
    scaling="proportional"/>

  <welcome
    file="Welcome.txt"
    mime-type="text/plain"/>

  <license
    file="License.txt"
    mime-type="text/plain"/>

  <conclusion
    file="Conclusion.txt"
    mime-type="text/plain"/>

  <options
    customize="never"
    require-scripts="false"
    hostArchitectures="$expected_archs"/>

  <volume-check>
    <allowed-os-versions>
      <os-version min="$minimum_system"/>
    </allowed-os-versions>
  </volume-check>

  <choices-outline>
    <line choice="vitte-toolchain"/>
    <line choice="vitte-editors"/>
  </choices-outline>

  <choice
    id="vitte-toolchain"
    visible="false"
    title="Vitte systems language toolchain">
    <pkg-ref id="org.vitte.toolchain"/>
  </choice>

  <choice
    id="vitte-editors"
    visible="false"
    title="Vitte editor integrations">
    <pkg-ref id="org.vitte.editors"/>
  </choice>

  <pkg-ref
    id="org.vitte.toolchain"
    version="$VERSION"
    onConclusion="none">
    vitte-component.pkg
  </pkg-ref>

  <pkg-ref
    id="org.vitte.editors"
    version="$VERSION"
    onConclusion="none">
    vitte-editors.pkg
  </pkg-ref>
</installer-gui-script>
EOF
}

write_welcome() {
  output=$1
  architecture=$2
  minimum_system=$3

  cat > "$output" <<EOF
Vitte $VERSION

Architecture: $architecture
Minimum target system: Mac OS X $minimum_system

This installer adds the Vitte compiler, runtime, standard library,
diagnostic tools, editor support, and shell completions.

What is included:

- Compiler commands: vitte and vittec
- Installation doctor for checking the installed toolchain
- Runtime and standard library sources
- Shell completions and editor integrations

Editor support includes:

- Vim
- Emacs
- Nano
- Geany

Recognized file extensions:

- .vit
- .vitte
- .vitl

Editor files are installed in the directories actually read by each editor.
They are not only stored in the Vitte documentation directory.
EOF
}

write_conclusion() {
  output=$1
  cat > "$output" <<EOF
Vitte $VERSION is ready.

Open a new terminal and run:

  vitte --version
  vitte-installer-doctor

The compiler, runtime, standard library, editor integrations, and shell
completions are installed together. See the installation guide in the
installed documentation for editor-specific setup.
EOF
}

build_one() {
  label=$1
  binary=$2
  expected_archs=$3
  minimum_system=$4

  package_file=$OUT_DIR/vitte-$VERSION-macos-$label.pkg
  dmg_file=$OUT_DIR/vitte-$VERSION-macos-$label.dmg

  component_dir=$ROOT_DIR/target/macos-product-$label
  resources=$component_dir/resources
  components=$component_dir/components
  toolchain_component=$components/vitte-component.pkg
  editors_component=$components/vitte-editors.pkg
  distribution=$component_dir/Distribution.xml

  old_ifs=$IFS
  IFS=,

  for expected in $expected_archs; do
    check_arch "$binary" "$expected"
  done

  IFS=$old_ifs

  log "packaging $label from $binary"

  rm -rf "$component_dir"

  rm -f \
    "$package_file" \
    "$package_file.sha256" \
    "$dmg_file" \
    "$dmg_file.sha256"

  mkdir -p \
    "$resources" \
    "$components" \
    "$OUT_DIR"

  build_toolchain_component \
    "$component_dir" \
    "$toolchain_component" \
    "$label" \
    "$binary"

  require_file \
    "$toolchain_component" \
    "toolchain component package"

  build_editor_component \
    "$component_dir" \
    "$editors_component"

  require_file \
    "$editors_component" \
    "editor component package"

  copy_resources "$resources"

  write_distribution \
    "$distribution" \
    "$expected_archs" \
    "$minimum_system"

  write_welcome \
    "$resources/Welcome.txt" \
    "$expected_archs" \
    "$minimum_system"

  install -m 0644 "$ROOT_DIR/LICENSE" "$resources/License.txt"
  write_conclusion "$resources/Conclusion.txt"

  require_file \
    "$distribution" \
    "macOS Distribution XML"

  require_file \
    "$resources/Welcome.txt" \
    "macOS welcome resource"

  require_file "$resources/License.txt" "macOS license resource"
  require_file "$resources/Conclusion.txt" "macOS conclusion resource"

  log "building final package: $package_file"

  productbuild \
    --distribution "$distribution" \
    --resources "$resources" \
    --package-path "$components" \
    "$package_file"

  require_file \
    "$package_file" \
    "final macOS product package"

  verify_product_package \
    "$package_file" \
    "$label"

  sign_macos_artifact "$package_file"
  notarize_macos_artifact "$package_file"

  create_dmg \
    "$package_file" \
    "$dmg_file" \
    "Vitte-$VERSION-$label" \
    "$expected_archs" \
    "Mac OS X $minimum_system"

  require_file \
    "$package_file" \
    "final macOS product package"

  write_checksum "$package_file"
  if [ -s "$dmg_file" ]; then
    write_checksum "$dmg_file"
  fi
  scripts_build_write_artifact_manifest \
    "$package_file" macos "$label" "$VERSION" \
    "$package_file.MANIFEST.json" "$minimum_system"
  if [ -s "$dmg_file" ]; then
    scripts_build_write_artifact_manifest \
      "$dmg_file" macos "$label" "$VERSION" \
      "$dmg_file.MANIFEST.json" "$minimum_system"
  fi

  package_size=$(wc -c < "$package_file" | tr -d ' ')

  log "wrote $package_file ($package_size bytes)"
  if [ -s "$dmg_file" ]; then
    dmg_size=$(wc -c < "$dmg_file" | tr -d ' ')
    log "wrote $dmg_file ($dmg_size bytes)"
  else
    log "DMG artifact deferred for $label"
  fi
  log "toolchain component: $toolchain_component"
  log "editor component: $editors_component"
}

build_macos2006_i386_binary() {
  output=$MACOS2006_BINARY
  output_dir=$(dirname "$output")

  log "building Mac OS X 2006 i386 binary"

  rm -rf "$output_dir"
  mkdir -p "$output_dir"

  MACOSX_DEPLOYMENT_TARGET=$MACOS2006_DEPLOYMENT_TARGET \
  VITTE_MACOS_LEGACY_SDK=$MACOS2006_SDK \
  VITTE_MACOS_LEGACY_ARCHS=i386 \
  VITTE_MACOS_LEGACY_CC=$MACOS2006_CC \
  VITTE_MACOS_LEGACY_OUT=$output \
    make -C "$ROOT_DIR" macos2006-i386-bin

  require_file \
    "$output" \
    "Mac OS X 2006 i386 Vitte binary"

  check_arch "$output" i386
}

create_macos2006_profile() {
  stage=$ROOT_DIR/target/macos2006-profile
  archive=$OUT_DIR/vitte-$VERSION-macos2006-i386-config.tar.gz

  rm -rf "$stage"
  rm -f "$archive" "$archive.sha256"

  mkdir -p "$stage"

  install -m 0644 "$LOGO_FILE" "$stage/logo.png"

  cat > "$stage/MacOS2006.conf" <<EOF
MACOSX_DEPLOYMENT_TARGET=$MACOS2006_DEPLOYMENT_TARGET
VITTE_MACOS_LEGACY_SDK=$MACOS2006_SDK
VITTE_MACOS_LEGACY_ARCHS=i386
VITTE_MACOS_LEGACY_CC=$MACOS2006_CC
VITTE_MACOS_LEGACY_OUT=target/macos2006-i386/vitte
EOF

  cat > "$stage/README.txt" <<EOF
Vitte $VERSION historical Mac OS X 2006 i386 build profile

Architecture: i386
Deployment target: Mac OS X $MACOS2006_DEPLOYMENT_TARGET
SDK: $MACOS2006_SDK
Compiler: $MACOS2006_CC

Build binary:

    make macos2006-i386-bin

Build installer:

    ARCH=macos2006-i386 scripts/build-macos-installers.sh

The installer contains native syntax highlighting for Vim, Emacs, Nano
and Geany.
EOF

  scripts_build_tar_gz "$archive" "$stage" .

  require_file \
    "$archive" \
    "Mac OS X 2006 build profile archive"

  write_checksum "$archive"
}

build_macos2006_installer() {
  build_macos2006_i386_binary

  build_one \
    macos2006-i386 \
    "$MACOS2006_BINARY" \
    i386 \
    "$MACOS2006_DEPLOYMENT_TARGET"
}

maybe_build_macos2006_installer() {
  if command -v "$MACOS2006_CC" >/dev/null 2>&1; then
    build_macos2006_installer
  else
    log "Mac OS X 2006 i386 installer deferred: missing legacy compiler $MACOS2006_CC"
    log "Mac OS X 2006 configuration profile was still generated"
  fi
}

list_generated_artifacts() {
  log "generated artifacts:"

  found=0

  for artifact in \
    "$OUT_DIR"/vitte-"$VERSION"-macos-*.pkg \
    "$OUT_DIR"/vitte-"$VERSION"-macos-*.pkg.sha256 \
    "$OUT_DIR"/vitte-"$VERSION"-macos-*.dmg \
    "$OUT_DIR"/vitte-"$VERSION"-macos-*.dmg.sha256 \
    "$OUT_DIR"/vitte-"$VERSION"-macos2006-i386-config.tar.gz \
    "$OUT_DIR"/vitte-"$VERSION"-macos2006-i386-config.tar.gz.sha256
  do
    if [ -f "$artifact" ]; then
      printf '  - %s\n' "$artifact"
      found=1
    fi
  done

  [ "$found" -eq 1 ] ||
    die "no macOS package artifact was generated in $OUT_DIR"
}

[ "$(uname -s)" = Darwin ] ||
  die "macOS installers require a Darwin host"

for tool in \
  cp \
  find \
  grep \
  hdiutil \
  install \
  lipo \
  make \
  pkgbuild \
  pkgutil \
  productbuild \
  tar \
  tr \
  wc
do
  require "$tool"
done

require_directory "$EDITORS_DIR" "editor integrations"
require_directory "$COMPLETIONS_DIR" "shell completions"
require_file "$LOGO_FILE" "Vitte logo"

mkdir -p "$OUT_DIR"

case "$ARCH" in
  all | arm64 | ARM64 | x86_64 | X86_64 | universal | universal2 | i386 | macos2006-i386)
    ;;
  amd64 | AMD64)
    ARCH=x86_64
    ;;
  aarch64 | AArch64 | AARCH64)
    ARCH=arm64
    ;;
  x86)
    ARCH=macos2006-i386
    ;;
  *)
    die "unsupported macOS architecture: $ARCH"
    ;;
esac

create_macos2006_profile

case "$ARCH" in
  all | universal | universal2)
    make -C "$ROOT_DIR" macos-universal-bin
    ;;
  arm64)
    make -C "$ROOT_DIR" macos-arm64-bin
    ;;
  x86_64)
    make -C "$ROOT_DIR" macos-x86_64-bin
    ;;
esac

case "$ARCH" in
  all | arm64)
    build_one \
      arm64 \
      "$ROOT_DIR/target/macos-arm64/vitte" \
      arm64 \
      11.0
    ;;
esac

case "$ARCH" in
  all | x86_64)
    build_one \
      x86_64 \
      "$ROOT_DIR/target/macos-x86_64/vitte" \
      x86_64 \
      10.13
    ;;
esac

case "$ARCH" in
  all | universal)
    build_one \
      universal \
      "$ROOT_DIR/target/universal/vitte" \
      arm64,x86_64 \
      11.0
    ;;
esac

case "$ARCH" in
  all | universal2)
    build_one \
      universal2 \
      "$ROOT_DIR/target/universal/vitte" \
      arm64,x86_64 \
      11.0
    ;;
esac

case "$ARCH" in
  all)
    maybe_build_macos2006_installer
    ;;
esac

case "$ARCH" in
  i386 | macos2006-i386)
    build_macos2006_installer
    ;;
esac

list_generated_artifacts

log "complete version=$VERSION arch=$ARCH out=$OUT_DIR"
