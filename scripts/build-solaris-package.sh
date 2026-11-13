#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-solaris-package
. "$ROOT_DIR/scripts/common.sh"
scripts_build_parse_common_flags "$@"
VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}
case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac
ARCH=${ARCH:-all}
PACKAGE_NAME=${PACKAGE_NAME:-vitte}
SVR4_PACKAGE=${SVR4_PACKAGE:-VITTE}
REQUIRE_NATIVE=${STRICT_NATIVE:-${RELEASE_INSTALLER_GATE:-0}}

EDITORS_DIR=$ROOT_DIR/editors
COMPLETIONS_DIR=$ROOT_DIR/completions
LICENSE_FILE=$ROOT_DIR/LICENSE
LOGO_FILE=$ROOT_DIR/assets/logo.png
PAYLOAD_SCRIPT=$ROOT_DIR/scripts/stage-installer-payload.sh
scripts_build_maybe_help "usage: build-solaris-package.sh [--dry-run]"

if [ "$LIST_TARGETS" -eq 1 ]; then
  printf '%s\n' 'solaris i386' 'solaris amd64'
  exit 0
fi

if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'VERSION=%s\n' "$VERSION"
  printf 'OUT_DIR=%s\n' "$OUT_DIR"
  printf 'ARCH=%s\n' "$ARCH"
  printf 'PACKAGE_NAME=%s\n' "$PACKAGE_NAME"
  printf 'SVR4_PACKAGE=%s\n' "$SVR4_PACKAGE"
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

scripts_build_maybe_dry_run "would build Solaris SVR4 packages version=$VERSION arch=$ARCH out=$OUT_DIR"

die() {
  printf '[build-solaris-package][error] %s\n' "$*" >&2
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

copy_tree() {
  source=$1
  destination=$2
  description=${3:-directory}

  [ -d "$source" ] ||
    die "$description directory not found: $source"

  scripts_build_copy_tree "$source" "$destination"

  printf '[build-solaris-package] added %s: %s\n' \
    "$description" \
    "$destination"
}

verify_license() {
  license_file=$1

  require_file "$license_file" "VitteFoundation Public License"

  grep -F 'VITTEFOUNDATION PUBLIC LICENSE' "$license_file" >/dev/null ||
    die "invalid license: missing VitteFoundation Public License title"

  grep -F 'Version 1.0' "$license_file" >/dev/null ||
    die "invalid license: expected VitteFoundation Public License version 1.0"
}

add_integrations() {
  data_root=$1
  share_root=$data_root/usr/local/share/vitte

  copy_tree \
    "$EDITORS_DIR" \
    "$share_root/editors" \
    "editor integrations"

  copy_tree \
    "$COMPLETIONS_DIR" \
    "$share_root/completions" \
    "shell completions"
}

add_license_and_logo() {
  data_root=$1

  license_destination=$data_root/usr/local/share/licenses/$PACKAGE_NAME
  assets_destination=$data_root/usr/local/share/vitte/assets

  mkdir -p \
    "$license_destination" \
    "$assets_destination"

  install -m 0644 \
    "$LICENSE_FILE" \
    "$license_destination/LICENSE"

  install -m 0644 \
    "$LOGO_FILE" \
    "$assets_destination/logo.png"

  printf '[build-solaris-package] added VitteFoundation Public License: %s\n' \
    "$license_destination/LICENSE"

  printf '[build-solaris-package] added Vitte logo: %s\n' \
    "$assets_destination/logo.png"
}

verify_payload() {
  data_root=$1
  architecture=$2

  [ -x "$data_root/usr/local/bin/vitte" ] ||
    die "missing or non-executable Vitte command for Solaris $architecture"

  scripts_build_verify_modules "$data_root/usr/local/share/vitte"

  require_directory \
    "$data_root/usr/local/share/vitte/editors" \
    "packaged editor integrations"

  require_directory \
    "$data_root/usr/local/share/vitte/completions" \
    "packaged shell completions"

  require_file \
    "$data_root/usr/local/share/vitte/tools/install-unix-integrations.sh" \
    "editor and shell integration installer"

  verify_license \
    "$data_root/usr/local/share/licenses/$PACKAGE_NAME/LICENSE"

  require_file \
    "$data_root/usr/local/share/vitte/assets/logo.png" \
    "packaged Vitte logo"
}

solaris_arch_name() {
  architecture=$1

  case "$architecture" in
    amd64)
      printf 'i386'
      ;;
    i386)
      printf 'i386'
      ;;
    *)
      die "unsupported Solaris architecture: $architecture"
      ;;
  esac
}

write_pkginfo() {
  output=$1
  architecture=$2
  svr4_arch=$3

  cat > "$output" <<EOF
PKG=$SVR4_PACKAGE
NAME=Vitte systems language toolchain
ARCH=$svr4_arch
VERSION=$VERSION
CATEGORY=application,development
VENDOR=Vitte Team
EMAIL=maintainers@vitte-lang.org
HOTLINE=https://vitte-lang.org/
BASEDIR=/
CLASSES=none
ISTATES=S s 1 2 3
RSTATES=S s 1 2 3
MAXINST=1
VITTE_PROCESSOR=$architecture
DESC=Processor-specific Vitte compiler, runtime, standard library, documentation, editor integrations, shell completions, VitteFoundation Public License, visual assets, and tools.
EOF
}

write_depend() {
  output=$1

  cat > "$output" <<'EOF'
P SUNWbash GNU Bourne-Again shell
P SUNWpython Python runtime
EOF
}

write_postinstall() {
  output=$1

  cat > "$output" <<'EOF'
#!/bin/sh
set -eu

for command in \
  /usr/local/bin/vitte \
  /usr/local/bin/vittec \
  /usr/local/bin/vitte \
  /usr/local/libexec/vitte/vitte \
  /usr/local/libexec/vitte/vittec
do
  if [ -f "$command" ]; then
    chmod 0755 "$command"
  fi
done

integration_helper=/usr/local/share/vitte/tools/install-unix-integrations.sh
if [ -x "$integration_helper" ]; then
  PREFIX=/usr/local "$integration_helper"
fi

printf '%s\n' 'Vitte installation completed.'
printf '%s\n' 'Run: /usr/local/bin/vitte --help'

exit 0
EOF

  chmod 0755 "$output"
}

write_preremove() {
  output=$1

  cat > "$output" <<'EOF'
#!/bin/sh
set -eu

exit 0
EOF

  chmod 0755 "$output"
}

generate_prototype() {
  data_root=$1
  metadata=$2

  python3 - "$data_root" "$metadata" <<'PY'
import os
import stat
import sys
from pathlib import Path

root = Path(sys.argv[1])
metadata = Path(sys.argv[2])
prototype = metadata / "prototype"

lines = [
    "i pkginfo",
    "i depend",
    "i postinstall",
    "i preremove",
]

for path in sorted(root.rglob("*")):
    relative = path.relative_to(root).as_posix()
    mode = stat.S_IMODE(path.lstat().st_mode)

    if path.is_symlink():
        lines.append(f"s none {relative}={os.readlink(path)}")
    elif path.is_dir():
        lines.append(f"d none {relative} {mode:04o} root sys")
    elif path.is_file():
        owner_group = "root bin"

        if relative.startswith("usr/local/share/"):
            owner_group = "root sys"

        lines.append(
            f"f none {relative} {mode:04o} {owner_group}"
        )

prototype.write_text(
    "\n".join(lines) + "\n",
    encoding="utf-8",
)
PY
}

verify_prototype() {
  prototype=$1

  grep -F 'i pkginfo' "$prototype" >/dev/null ||
    die "prototype is missing pkginfo"

  grep -F 'i depend' "$prototype" >/dev/null ||
    die "prototype is missing depend"

  grep -F 'i postinstall' "$prototype" >/dev/null ||
    die "prototype is missing postinstall"

  grep -F 'i preremove' "$prototype" >/dev/null ||
    die "prototype is missing preremove"

  grep -E '^[fs] none usr/local/bin/vitte([ =]|$)' "$prototype" >/dev/null ||
    die "prototype is missing the Vitte command"

  grep -F 'usr/local/share/vitte/editors/' "$prototype" >/dev/null ||
    die "prototype is missing editor integrations"

  grep -F 'usr/local/share/vitte/completions/' "$prototype" >/dev/null ||
    die "prototype is missing shell completions"

  grep -F 'usr/local/share/licenses/vitte/LICENSE' "$prototype" >/dev/null ||
    die "prototype is missing the VitteFoundation license"

  grep -F 'usr/local/share/vitte/assets/logo.png' "$prototype" >/dev/null ||
    die "prototype is missing the Vitte logo"
}

create_portable_kit() {
  spool=$1
  kit_file=$2

  rm -f "$kit_file"

  cat > "$spool/install.sh" <<'SH'
#!/bin/sh
set -eu
PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
case "$PREFIX" in /) echo "PREFIX must not be /" >&2; exit 1 ;; /*) ;; *) echo "PREFIX must be absolute: $PREFIX" >&2; exit 1 ;; esac
case "$PREFIX" in */) PREFIX=${PREFIX%/};; esac
case "$PREFIX/" in */../*|*/./*|*//*) echo "PREFIX must not contain dot or empty path components: $PREFIX" >&2; exit 1 ;; esac
if [ -n "$DESTDIR" ]; then
  case "$DESTDIR" in /) echo "DESTDIR must not be /" >&2; exit 1 ;; /*) ;; *) echo "DESTDIR must be absolute: $DESTDIR" >&2; exit 1 ;; esac
  case "$DESTDIR" in */) DESTDIR=${DESTDIR%/};; esac
  case "$DESTDIR/" in */../*|*/./*|*//*) echo "DESTDIR must not contain dot or empty path components: $DESTDIR" >&2; exit 1 ;; esac
fi
target_root=${DESTDIR:-/}
target_prefix=$target_root$PREFIX
check_path() {
  current=$1
  while :; do
    [ ! -L "$current" ] || { echo "refusing to follow a symbolic link: $current" >&2; exit 1; }
    [ "$current" != / ] || break
    parent=$(dirname "$current")
    [ "$parent" != "$current" ] || break
    current=$parent
  done
}
check_path "$target_prefix"
mkdir -p "$target_prefix" 2>/dev/null || { echo "cannot create installation prefix: $target_prefix" >&2; exit 1; }
[ -n "$DESTDIR" ] || [ "$(id -u)" -eq 0 ] || [ -w "$target_prefix" ] || { echo "Solaris portable install requires root or a writable PREFIX" >&2; exit 1; }
if [ "$PREFIX" = /usr/local ]; then
  cp -pR "$(dirname "$0")/root/." "$target_root/"
else
  cp -pR "$(dirname "$0")/root/usr/local/." "$target_prefix/"
fi
integration_helper=$target_prefix/share/vitte/tools/install-unix-integrations.sh
[ -x "$integration_helper" ] || { echo "Vitte integration installer is missing: $integration_helper" >&2; exit 1; }
PREFIX=$PREFIX DESTDIR=$DESTDIR "$integration_helper"
echo "Vitte installed in $DESTDIR$PREFIX."
SH
  chmod 0755 "$spool/install.sh"
  cat > "$spool/uninstall.sh" <<'SH'
#!/bin/sh
set -eu
PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
case "$PREFIX" in /) echo "PREFIX must not be /" >&2; exit 1 ;; /*) ;; *) echo "PREFIX must be absolute: $PREFIX" >&2; exit 1 ;; esac
case "$PREFIX" in */) PREFIX=${PREFIX%/};; esac
case "$PREFIX/" in */../*|*/./*|*//*) echo "PREFIX must not contain dot or empty path components: $PREFIX" >&2; exit 1 ;; esac
if [ -n "$DESTDIR" ]; then
  case "$DESTDIR" in /) echo "DESTDIR must not be /" >&2; exit 1 ;; /*) ;; *) echo "DESTDIR must be absolute: $DESTDIR" >&2; exit 1 ;; esac
  case "$DESTDIR" in */) DESTDIR=${DESTDIR%/};; esac
  case "$DESTDIR/" in */../*|*/./*|*//*) echo "DESTDIR must not contain dot or empty path components: $DESTDIR" >&2; exit 1 ;; esac
fi
root=${DESTDIR:-/}
target_prefix=$root$PREFIX
check_path() {
  current=$1
  while :; do
    [ ! -L "$current" ] || { echo "refusing to follow a symbolic link: $current" >&2; exit 1; }
    [ "$current" != / ] || break
    parent=$(dirname "$current")
    [ "$parent" != "$current" ] || break
    current=$parent
  done
}
check_path "$target_prefix"
check_path "$target_prefix/bin"
check_path "$target_prefix/libexec/vitte"
check_path "$target_prefix/share/vitte"
[ -n "$DESTDIR" ] || [ "$(id -u)" -eq 0 ] || [ -w "$target_prefix" ] || { echo "Solaris portable uninstall requires root or a writable PREFIX" >&2; exit 1; }
rm -f "$target_prefix/bin/vitte" "$target_prefix/bin/vittec" "$target_prefix/bin/vitte-installer-doctor"
integration_helper=$target_prefix/share/vitte/tools/install-unix-integrations.sh
if [ -x "$integration_helper" ]; then PREFIX=$PREFIX DESTDIR=$DESTDIR "$integration_helper" uninstall; fi
rm -rf "$target_prefix/libexec/vitte" "$target_prefix/share/vitte"
echo "Vitte removed from $DESTDIR$PREFIX."
SH
  chmod 0755 "$spool/uninstall.sh"

  scripts_build_tar_gz "$kit_file" "$spool" pkginfo depend postinstall preremove prototype install.sh uninstall.sh root

  [ -s "$kit_file" ] ||
    die "portable SVR4 kit was not created: $kit_file"
}

verify_portable_kit() {
  kit_file=$1

  listing=$(mktemp)
  trap 'rm -f "$listing"' EXIT HUP INT TERM

  tar -tzf "$kit_file" > "$listing"

  for required in \
    pkginfo \
    depend \
    postinstall \
    preremove \
    prototype \
    install.sh \
    uninstall.sh \
    root/usr/local/bin/vitte \
    root/usr/local/share/licenses/vitte/LICENSE \
    root/usr/local/share/vitte/assets/logo.png
  do
    grep -Fx "$required" "$listing" >/dev/null ||
      die "portable SVR4 kit is missing: $required"
  done

  grep -q '^root/usr/local/share/vitte/editors/' "$listing" ||
    die "portable SVR4 kit is missing editor integrations"

  grep -q '^root/usr/local/share/vitte/completions/' "$listing" ||
    die "portable SVR4 kit is missing shell completions"

  rm -f "$listing"
  trap - EXIT HUP INT TERM
}

write_checksum() {
  file=$1

  scripts_build_sha256_write "$file" "$file.sha256"

  printf '[build-solaris-package] wrote %s.sha256\n' "$file"
}

build_native_package() {
  data_root=$1
  metadata=$2
  stage=$3
  package_file=$4

  package_spool=$stage/package-spool

  rm -rf "$package_spool"
  rm -f "$package_file"

  mkdir -p "$package_spool"

  (
    cd "$metadata"

    pkgmk \
      -o \
      -r "$data_root" \
      -d "$package_spool" \
      -f "$metadata/prototype"
  )

  pkgtrans \
    -s \
    "$package_spool" \
    "$package_file" \
    "$SVR4_PACKAGE"

  [ -s "$package_file" ] ||
    die "pkgtrans did not create $package_file"

  write_checksum "$package_file"

  printf '[build-solaris-package] wrote %s (%s bytes)\n' \
    "$package_file" \
    "$(wc -c < "$package_file" | tr -d ' ')"
}

build_one() {
  architecture=$1
  svr4_arch=$(solaris_arch_name "$architecture")

  stage=$ROOT_DIR/target/installer-solaris-$architecture
  metadata=$stage/metadata
  data_root=$stage/root
  spool=$stage/spool

  package_file=$OUT_DIR/${PACKAGE_NAME}-${VERSION}-solaris-${architecture}.pkg
  kit_file=$OUT_DIR/${PACKAGE_NAME}-${VERSION}-solaris-${architecture}-spool.tar.gz

  printf '[build-solaris-package] building Solaris %s package\n' \
    "$architecture"

  rm -rf "$stage"
  rm -f \
    "$package_file" \
    "$package_file.sha256" \
    "$kit_file" \
    "$kit_file.sha256"

  mkdir -p \
    "$metadata" \
    "$data_root" \
    "$spool/root" \
    "$OUT_DIR"

  VERSION=$VERSION \
    "$PAYLOAD_SCRIPT" \
    "$data_root" \
    solaris \
    "$architecture" \
    unix

  add_integrations "$data_root"
  add_license_and_logo "$data_root"
  verify_payload "$data_root" "$architecture"

  write_pkginfo \
    "$metadata/pkginfo" \
    "$architecture" \
    "$svr4_arch"

  write_depend "$metadata/depend"
  write_postinstall "$metadata/postinstall"
  write_preremove "$metadata/preremove"

  generate_prototype "$data_root" "$metadata"
  verify_prototype "$metadata/prototype"

  install -m 0644 "$metadata/pkginfo" "$spool/pkginfo"
  install -m 0644 "$metadata/depend" "$spool/depend"
  install -m 0755 "$metadata/postinstall" "$spool/postinstall"
  install -m 0755 "$metadata/preremove" "$spool/preremove"
  install -m 0644 "$metadata/prototype" "$spool/prototype"

  copy_tree \
    "$data_root" \
    "$spool/root" \
    "Solaris payload"

  create_portable_kit "$spool" "$kit_file"
  verify_portable_kit "$kit_file"
  write_checksum "$kit_file"

  printf '[build-solaris-package] wrote build kit %s (%s bytes)\n' \
    "$kit_file" \
    "$(wc -c < "$kit_file" | tr -d ' ')"

  if command -v pkgmk >/dev/null 2>&1 &&
     command -v pkgtrans >/dev/null 2>&1
  then
    build_native_package \
      "$data_root" \
      "$metadata" \
      "$stage" \
      "$package_file"
  else
    [ "$REQUIRE_NATIVE" -eq 0 ] ||
      die "release requires native Solaris pkgmk and pkgtrans for $architecture"
    printf '%s\n' \
      '[build-solaris-package] pkgmk/pkgtrans unavailable; portable SVR4 kit generated, native .pkg deferred' \
      >&2
  fi

  printf '[build-solaris-package] completed Solaris %s\n' \
    "$architecture"
}

[ -x "$PAYLOAD_SCRIPT" ] ||
  die "payload staging script is missing or not executable: $PAYLOAD_SCRIPT"

require_directory "$EDITORS_DIR" "editor integrations"
require_directory "$COMPLETIONS_DIR" "shell completions"
verify_license "$LICENSE_FILE"
require_file "$LOGO_FILE" "Vitte logo"

for tool in \
  cp \
  find \
  grep \
  install \
  mktemp \
  python3 \
  tar \
  wc
do
  require "$tool"
done

case "$ARCH" in
  all)
    build_one amd64
    build_one i386
    ;;
  x86_64 | amd64)
    build_one amd64
    ;;
  i386 | i486 | i586 | i686 | x86)
    build_one i386
    ;;
  *)
    die "unsupported Solaris architecture: $ARCH"
    ;;
esac

printf '[build-solaris-package] complete version=%s arch=%s out=%s\n' \
  "$VERSION" \
  "$ARCH" \
  "$OUT_DIR"
