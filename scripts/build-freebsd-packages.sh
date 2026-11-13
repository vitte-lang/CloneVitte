#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

SCRIPT_NAME=build-freebsd-packages

. "$ROOT_DIR/scripts/common.sh"

scripts_build_parse_common_flags "$@"

VERSION=${VERSION:-$(scripts_build_package_version)}

OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}

case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac

ARCH=${ARCH:-all}

PACKAGE_NAME=${PACKAGE_NAME:-vitte}

FREEBSD_MAJOR=${FREEBSD_MAJOR:-14}



EDITORS_DIR=$ROOT_DIR/editors

COMPLETIONS_DIR=$ROOT_DIR/completions

LICENSE_FILE=$ROOT_DIR/LICENSE

LOGO_FILE=$ROOT_DIR/assets/logo.png



PAYLOAD_SCRIPT=$ROOT_DIR/scripts/stage-installer-payload.sh

scripts_build_maybe_help "usage: build-freebsd-packages.sh [--dry-run]"

if [ "$LIST_TARGETS" -eq 1 ]; then
  printf '%s\n' \
    'freebsd amd64' \
    'freebsd i386' \
    'freebsd arm64' \
    'freebsd armv7' \
    'freebsd armv6' \
    'freebsd riscv64' \
    'freebsd powerpc' \
    'freebsd powerpc64' \
    'freebsd powerpc64le'
  exit 0
fi

if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'VERSION=%s\n' "$VERSION"
  printf 'OUT_DIR=%s\n' "$OUT_DIR"
  printf 'ARCH=%s\n' "$ARCH"
  printf 'PACKAGE_NAME=%s\n' "$PACKAGE_NAME"
  printf 'FREEBSD_MAJOR=%s\n' "$FREEBSD_MAJOR"
  exit 0
fi

if [ -n "$VERIFY_ONLY" ]; then
  verify_dir=$VERIFY_ONLY
  case "$verify_dir" in /*) ;; *) verify_dir=$ROOT_DIR/$verify_dir;; esac
  VERSION=$VERSION OUT_DIR=$verify_dir VERIFY_METADATA=1 \
    "$ROOT_DIR/scripts/verify-installers.sh"
  exit 0
fi

[ "$CLEAN" -eq 0 ] ||
  scripts_build_die 'use build-all-installers.sh --clean to remove installer staging directories'

scripts_build_maybe_dry_run "would build FreeBSD pkg artifacts version=$VERSION arch=$ARCH out=$OUT_DIR"



die() {

  printf '[build-freebsd-packages][error] %s\n' "$*" >&2

  exit 1

}



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



copy_tree() {

  source_dir=$1

  destination_dir=$2

  description=$3



  require_directory "$source_dir" "$description"



  mkdir -p "$destination_dir"

  cp -R "$source_dir"/. "$destination_dir"/



  printf '[build-freebsd-packages] added %s: %s\n' \
    "$description" \
    "$destination_dir"

}



verify_license_file() {

  license_file=$1



  require_file "$license_file" "VitteFoundation Public License"



  grep -F 'VITTEFOUNDATION PUBLIC LICENSE' "$license_file" >/dev/null ||

    die "invalid VitteFoundation license file: missing license title"



  grep -F 'Version 1.0' "$license_file" >/dev/null ||

    die "invalid VitteFoundation license file: expected version 1.0"

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
    "Vim Vitte syntax" \
    "$EDITORS_DIR/vim/syntax/vitte.vim" \
    "$EDITORS_DIR/vim/vitte.vim" \
    "$EDITORS_DIR/vitte.vim")



  vim_root=$data_root/usr/local/share/vim/vimfiles



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



  printf '[build-freebsd-packages] installed Vim syntax integration\n'

}



install_emacs_integration() {

  data_root=$1



  emacs_mode=$(find_first_file \
    "Emacs Vitte mode" \
    "$EDITORS_DIR/emacs/vitte-mode.el" \
    "$EDITORS_DIR/emacs/vitte.el" \
    "$EDITORS_DIR/vitte-mode.el")



  emacs_root=$data_root/usr/local/share/emacs/site-lisp

  startup_root=$data_root/usr/local/share/emacs/site-lisp/site-start.d



  mkdir -p \
    "$emacs_root" \
    "$startup_root"



  install -m 0644 \
    "$emacs_mode" \
    "$emacs_root/vitte-mode.el"



  cat > "$startup_root/vitte-init.el" <<'EOF'

;;; vitte-init.el --- system-wide Vitte mode registration



(add-to-list 'load-path "/usr/local/share/emacs/site-lisp")



(autoload 'vitte-mode

  "vitte-mode"

  "Major mode for the Vitte programming language."

  t)



(add-to-list 'auto-mode-alist '("\\\\.vit\\\\'" . vitte-mode))

(add-to-list 'auto-mode-alist '("\\\\.vitte\\\\'" . vitte-mode))

(add-to-list 'auto-mode-alist '("\\\\.vitl\\\\'" . vitte-mode))



(provide 'vitte-system-init)



;;; vitte-init.el ends here

EOF



  chmod 0644 "$startup_root/vitte-init.el"



  printf '[build-freebsd-packages] installed Emacs mode integration\n'

}



install_nano_integration() {

  data_root=$1



  nano_syntax=$(find_first_file \
    "Nano Vitte syntax" \
    "$EDITORS_DIR/nano/vitte.nanorc" \
    "$EDITORS_DIR/nano/vitte.nano" \
    "$EDITORS_DIR/vitte.nanorc")



  nano_root=$data_root/usr/local/share/nano



  mkdir -p "$nano_root"



  install -m 0644 \
    "$nano_syntax" \
    "$nano_root/vitte.nanorc"



  printf '[build-freebsd-packages] installed Nano syntax integration\n'

}



install_geany_integration() {

  data_root=$1



  geany_definition=$(find_first_file \
    "Geany Vitte filetype" \
    "$EDITORS_DIR/geany/filetypes.Vitte.conf" \
    "$EDITORS_DIR/geany/filetypes.vitte.conf" \
    "$EDITORS_DIR/geany/vitte.conf" \
    "$EDITORS_DIR/filetypes.Vitte.conf")



  geany_root=$data_root/usr/local/share/geany/filedefs



  mkdir -p "$geany_root"



  install -m 0644 \
    "$geany_definition" \
    "$geany_root/filetypes.Vitte.conf"



  printf '[build-freebsd-packages] installed Geany filetype integration\n'

}



install_editor_integrations() {

  data_root=$1



  add_archived_integrations "$data_root"

  install_vim_integration "$data_root"

  install_emacs_integration "$data_root"

  install_nano_integration "$data_root"

  install_geany_integration "$data_root"

  completion_root=$data_root/usr/local/share/vitte/completions
  bash_destination=$data_root/usr/local/share/bash-completion/completions/vitte
  fish_destination=$data_root/usr/local/share/fish/vendor_completions.d/vitte.fish
  zsh_destination=$data_root/usr/local/share/zsh/site-functions/_vitte
  mkdir -p \
    "$(dirname "$bash_destination")" \
    "$(dirname "$fish_destination")" \
    "$(dirname "$zsh_destination")"
  install -m 0644 "$completion_root/bash/vitte" "$bash_destination"
  install -m 0644 "$completion_root/fish/vitte.fish" "$fish_destination"
  install -m 0644 "$completion_root/zsh/_vitte" "$zsh_destination"
  printf '%s\n' \
    '[build-freebsd-packages] installed Bash, Fish and Zsh completions'

}



add_license_and_logo() {

  data_root=$1



  license_root=$data_root/usr/local/share/licenses/$PACKAGE_NAME

  assets_root=$data_root/usr/local/share/vitte/assets



  mkdir -p \
    "$license_root" \
    "$assets_root"



  install -m 0644 \
    "$LICENSE_FILE" \
    "$license_root/LICENSE"



  install -m 0644 \
    "$LOGO_FILE" \
    "$assets_root/logo.png"



  printf '[build-freebsd-packages] added VitteFoundation Public License\n'

  printf '[build-freebsd-packages] added Vitte logo\n'

}



add_pkg_scripts() {

  metadata=$1



  cat > "$metadata/+POST_INSTALL" <<'EOF'

#!/bin/sh

set -eu



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



ensure_nano_include \
  /usr/local/etc/nanorc \
  /usr/local/share/nano/vitte.nanorc



printf '%s\n' 'Vitte editor integrations installed:'

printf '%s\n' '  Vim:   *.vit, *.vitte, *.vitl'

printf '%s\n' '  Emacs: *.vit, *.vitte, *.vitl'

printf '%s\n' '  Nano:  *.vit, *.vitte, *.vitl'

printf '%s\n' '  Geany: filetypes.Vitte.conf'



exit 0

EOF



  chmod 0755 "$metadata/+POST_INSTALL"



  cat > "$metadata/+PRE_DEINSTALL" <<'EOF'

#!/bin/sh

set -eu



exit 0

EOF



  chmod 0755 "$metadata/+PRE_DEINSTALL"



  cat > "$metadata/+POST_DEINSTALL" <<'EOF'

#!/bin/sh

set -eu



nanorc=/usr/local/etc/nanorc

syntax_file=/usr/local/share/nano/vitte.nanorc

include_line="include \\"$syntax_file\\""



if [ -f "$nanorc" ]; then

  temporary_file="${nanorc}.vitte-tmp.$$"



  grep -Fv "$include_line" "$nanorc" > "$temporary_file" || true

  cat "$temporary_file" > "$nanorc"

  rm -f "$temporary_file"

fi



exit 0

EOF



  chmod 0755 "$metadata/+POST_DEINSTALL"

}



verify_payload() {

  data_root=$1

  arch=$2



  [ -x "$data_root/usr/local/bin/vitte" ] ||

    die "missing or non-executable Vitte command for FreeBSD $arch"



  scripts_build_verify_modules "$data_root/usr/local/share/vitte"



  require_directory \
    "$data_root/usr/local/share/vitte/editors" \
    "archived editor integrations"



  require_directory \
    "$data_root/usr/local/share/vitte/completions" \
    "shell completions"

  require_file \
    "$data_root/usr/local/share/bash-completion/completions/vitte" \
    "Bash Vitte completion"

  require_file \
    "$data_root/usr/local/share/fish/vendor_completions.d/vitte.fish" \
    "Fish Vitte completion"

  require_file \
    "$data_root/usr/local/share/zsh/site-functions/_vitte" \
    "Zsh Vitte completion"



  require_file \
    "$data_root/usr/local/share/vim/vimfiles/syntax/vitte.vim" \
    "Vim Vitte syntax"



  require_file \
    "$data_root/usr/local/share/vim/vimfiles/ftdetect/vitte.vim" \
    "Vim Vitte filetype detection"



  require_file \
    "$data_root/usr/local/share/vim/vimfiles/ftplugin/vitte.vim" \
    "Vim Vitte filetype plugin"



  require_file \
    "$data_root/usr/local/share/emacs/site-lisp/vitte-mode.el" \
    "Emacs Vitte mode"



  require_file \
    "$data_root/usr/local/share/emacs/site-lisp/site-start.d/vitte-init.el" \
    "Emacs Vitte automatic loader"



  require_file \
    "$data_root/usr/local/share/nano/vitte.nanorc" \
    "Nano Vitte syntax"



  require_file \
    "$data_root/usr/local/share/geany/filedefs/filetypes.Vitte.conf" \
    "Geany Vitte filetype"



  verify_license_file \
    "$data_root/usr/local/share/licenses/$PACKAGE_NAME/LICENSE"



  require_file \
    "$data_root/usr/local/share/vitte/assets/logo.png" \
    "Vitte logo"

}



generate_manifest() {

  data_root=$1

  metadata=$2

  package_name=$3

  version=$4

  abi=$5



  python3 - \
    "$data_root" \
    "$metadata" \
    "$package_name" \
    "$version" \
    "$abi" <<'PY'

import hashlib

import json

import sys

from pathlib import Path



data_root = Path(sys.argv[1])

metadata = Path(sys.argv[2])

name = sys.argv[3]

version = sys.argv[4]

abi = sys.argv[5]



files: dict[str, str] = {}
directories: set[str] = set()

flat_size = 0



for path in sorted(data_root.rglob("*")):
    relative = "/" + path.relative_to(data_root).as_posix()

    if path.is_symlink():
        target = path.readlink().as_posix()
        files[relative] = "-"
        flat_size += len(target.encode("utf-8"))
        continue

    if path.is_dir():
        directories.add(relative)
        continue



    digest = hashlib.sha256(path.read_bytes()).hexdigest()

    files[relative] = "1$" + digest

    flat_size += path.stat().st_size



manifest = {

    "name": name,

    "version": version,

    "origin": "devel/vitte",

    "comment": "Vitte systems programming language toolchain",

    "maintainer": "maintainers@vitte-lang.org",

    "www": "https://vitte-lang.org/",

    "abi": abi,

    "arch": abi,

    "prefix": "/usr/local",

    "flatsize": flat_size,

    "desc": (

        "Processor-specific Vitte compiler, runtime, standard library, "

        "command-line tools, documentation, shell completions, "

        "VitteFoundation Public License and syntax highlighting for Vim, "

        "Emacs, Nano and Geany."

    ),

    "categories": [

        "devel",

        "lang",

    ],

    "licenses": [

        "VFPL-1.0",

    ],

    "annotations": {

        "vitte:editor-support": "vim,emacs,nano,geany",

        "vitte:file-extensions": "vit,vitte,vitl",

    },

    "files": files,
    "directories": {
        directory: "y"
        for directory in sorted(directories)
    },
}



compact_manifest = (

    json.dumps(

        manifest,

        ensure_ascii=True,

        separators=(",", ":"),

        sort_keys=True,

    )

    + "\n"

)



full_manifest = (

    json.dumps(

        manifest,

        ensure_ascii=True,

        indent=2,

        sort_keys=True,

    )

    + "\n"

)



(metadata / "+COMPACT_MANIFEST").write_text(

    compact_manifest,

    encoding="utf-8",

)



(metadata / "+MANIFEST").write_text(

    full_manifest,

    encoding="utf-8",

)

PY

}



verify_package() {
  package_file=$1
  package_listing=$(mktemp)
  normalized_listing=$(mktemp)

  trap 'rm -f "$package_listing" "$normalized_listing"' EXIT HUP INT TERM

  bsdtar -tf "$package_file" > "$package_listing"

  sed \
    -e 's#^\./##' \
    -e 's#^/##' \
    -e 's#/$##' \
    "$package_listing" > "$normalized_listing"

  # pkg create embeds install/deinstall scripts in package metadata.
  # Only the manifests are expected as archive entries here.
  for metadata_file in \
    +COMPACT_MANIFEST \
    +MANIFEST
  do
    grep -Fx "$metadata_file" "$normalized_listing" >/dev/null ||
      die "missing $metadata_file in package"
  done

  for required_file in \
    usr/local/bin/vitte \
    usr/local/bin/vittec
  do
    if ! grep -Fx "$required_file" "$normalized_listing" >/dev/null; then
      printf '[build-freebsd-packages][debug] archive entries containing vitte:\n' >&2
      grep -i 'vitte' "$normalized_listing" >&2 || true
      die "missing package file: /$required_file"
    fi
  done

  grep -F 'usr/local/share/vitte/editors/' "$normalized_listing" >/dev/null ||
    die "missing archived editor integrations in package"

  grep -F 'usr/local/share/vitte/completions/' "$normalized_listing" >/dev/null ||
    die "missing shell completions in package"

  rm -f "$package_listing" "$normalized_listing"
  trap - EXIT HUP INT TERM
}

write_checksum() {

  package_file=$1



  scripts_build_sha256_write "$package_file" "$package_file.sha256"

}



build_one() {

  arch=$1



  case "$arch" in

    amd64)

      freebsd_arch=amd64

      ;;

    arm64)

      freebsd_arch=aarch64

      ;;

    armv7)

      freebsd_arch=armv7

      ;;

    armv6)

      freebsd_arch=armv6

      ;;

    riscv64)

      freebsd_arch=riscv64

      ;;

    powerpc)

      freebsd_arch=powerpc

      ;;

    powerpc64)

      freebsd_arch=powerpc64

      ;;

    powerpc64le)

      freebsd_arch=powerpc64le

      ;;

    i386)

      freebsd_arch=i386

      ;;

    *)

      die "unsupported FreeBSD architecture: $arch"

      ;;

  esac



  abi=FreeBSD:$FREEBSD_MAJOR:$freebsd_arch

  stage=$ROOT_DIR/target/installer-freebsd-$arch

  metadata=$stage/metadata

  data_root=$stage/data

  package_file=$OUT_DIR/${PACKAGE_NAME}-${VERSION}-freebsd-${arch}.pkg

  checksum_file=$package_file.sha256



  printf '[build-freebsd-packages] building FreeBSD %s\n' "$arch"

  printf '[build-freebsd-packages] ABI: %s\n' "$abi"



  rm -rf "$stage"

  rm -f "$package_file" "$checksum_file"



  mkdir -p \
    "$metadata" \
    "$data_root" \
    "$OUT_DIR"



  VERSION=$VERSION \
    "$PAYLOAD_SCRIPT" \
    "$data_root" \
    freebsd \
    "$arch" \
    unix



  install_editor_integrations "$data_root"

  add_license_and_logo "$data_root"

  verify_payload "$data_root" "$arch"



  generate_manifest \
    "$data_root" \
    "$metadata" \
    "$PACKAGE_NAME" \
    "$VERSION" \
    "$abi"



  add_pkg_scripts "$metadata"



  # Build the FreeBSD package with pkg(8), using data_root as the
  # filesystem root. This keeps archive paths and manifest paths consistent.
  pkg create \
    -r "$data_root" \
    -m "$metadata" \
    -o "$OUT_DIR"

  # pkg(8) chooses its own output filename. Resolve it and rename it to the
  # deterministic Vitte artifact name expected by this script.
  created_package=$(find "$OUT_DIR" -maxdepth 1 -type f \
    \( -name "${PACKAGE_NAME}-${VERSION}.pkg" \
       -o -name "${PACKAGE_NAME}-${VERSION}.txz" \
       -o -name "${PACKAGE_NAME}-${VERSION}.tzst" \) \
    -print -quit)

  [ -n "$created_package" ] ||
    die "pkg create did not produce a package for $PACKAGE_NAME-$VERSION"

  if [ "$created_package" != "$package_file" ]; then
    mv -f "$created_package" "$package_file"
  fi

  verify_package "$package_file"

  write_checksum "$package_file"



  package_size=$(wc -c < "$package_file" | tr -d ' ')



  printf '[build-freebsd-packages] wrote %s (%s bytes)\n' \
    "$package_file" \
    "$package_size"



  printf '[build-freebsd-packages] wrote %s\n' \
    "$checksum_file"

}



[ -x "$PAYLOAD_SCRIPT" ] ||

  die "payload staging script is missing or not executable: $PAYLOAD_SCRIPT"



require_directory "$EDITORS_DIR" "editor integrations"

require_directory "$COMPLETIONS_DIR" "shell completions"

verify_license_file "$LICENSE_FILE"

require_file "$LOGO_FILE" "Vitte logo"



for tool in \
  bsdtar \
  pkg \
  mv \
  cat \
  chmod \
  cp \
  find \
  grep \
  install \
  mkdir \
  mktemp \
  python3 \
  wc

do

  require "$tool"

done



case "$ARCH" in

  all)

    for arch in amd64 i386 arm64 armv7 armv6 riscv64 powerpc powerpc64 powerpc64le; do

      build_one "$arch"

    done

    ;;

  x86_64 | X86_64 | amd64 | AMD64)

    build_one amd64

    ;;

  aarch64 | AArch64 | AARCH64 | arm64 | ARM64)

    build_one arm64

    ;;

  armv7 | armv7l | armhf)

    build_one armv7

    ;;

  armv6 | armel)

    build_one armv6

    ;;

  riscv64 | RISC-V64 | RISCV64)

    build_one riscv64

    ;;

  powerpc | ppc)

    build_one powerpc

    ;;

  powerpc64 | ppc64)

    build_one powerpc64

    ;;

  powerpc64le | ppc64le)

    build_one powerpc64le

    ;;

  i386 | i486 | i586 | i686 | x86)

    build_one i386

    ;;

  *)

    die "unsupported FreeBSD architecture: $ARCH"

    ;;

esac



printf '[build-freebsd-packages] complete version=%s arch=%s out=%s\n' \
  "$VERSION" \
  "$ARCH" \
  "$OUT_DIR"
