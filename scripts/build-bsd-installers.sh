#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-bsd-installers

. "$ROOT_DIR/scripts/common.sh"

scripts_build_parse_common_flags "$@"

VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}

case "$OUT_DIR" in
  /*) ;;
  *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;;
esac

ARCH=${ARCH:-all}
BSD_FAMILY=${BSD_FAMILY:-all}
BSD_RELEASE=${BSD_RELEASE:-all}

EDITORS_DIR=$ROOT_DIR/editors
COMPLETIONS_DIR=$ROOT_DIR/completions

scripts_build_maybe_help \
  "usage: build-bsd-installers.sh [--dry-run] [--list-targets] [--print-env]"

if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'VERSION=%s\n' "$VERSION"
  printf 'OUT_DIR=%s\n' "$OUT_DIR"
  printf 'ARCH=%s\n' "$ARCH"
  printf 'BSD_FAMILY=%s\n' "$BSD_FAMILY"
  printf 'BSD_RELEASE=%s\n' "$BSD_RELEASE"
  exit 0
fi

scripts_build_maybe_dry_run \
  "would build BSD portable installers version=$VERSION family=$BSD_FAMILY release=$BSD_RELEASE arch=$ARCH out=$OUT_DIR"

die() {
  scripts_build_die "$*"
}

copy_tree() {
  source_dir=$1
  destination_dir=$2
  description=$3

  [ -d "$source_dir" ] ||
    die "$description directory not found: $source_dir"

  mkdir -p "$destination_dir"

  scripts_build_copy_tree \
    "$source_dir" \
    "$destination_dir"

  printf '[build-bsd-installers] added %s: %s\n' \
    "$description" \
    "$destination_dir"
}

add_integrations() {
  root=$1

  vitte_share=$root/usr/local/share/vitte
  editors_destination=$vitte_share/editors
  completions_destination=$vitte_share/completions

  copy_tree \
    "$EDITORS_DIR" \
    "$editors_destination" \
    "editor integrations"

  copy_tree \
    "$COMPLETIONS_DIR" \
    "$completions_destination" \
    "shell completions"
}

verify_payload() {
  root=$1
  family=$2
  release=$3
  arch=$4

  [ -d "$root" ] ||
    die "payload root missing for $family $release $arch"

  [ -d "$root/usr/local" ] ||
    die "usr/local missing from $family $release $arch payload"

  scripts_build_verify_modules "$root/usr/local/share/vitte"

  [ -d "$root/usr/local/share/vitte/editors" ] ||
    die "editor integrations missing from $family $release $arch payload"

  [ -d "$root/usr/local/share/vitte/completions" ] ||
    die "shell completions missing from $family $release $arch payload"

  [ -x "$root/usr/local/share/vitte/tools/install-unix-integrations.sh" ] ||
    die "editor and shell integration installer missing from $family $release $arch payload"

  find "$root/usr/local/share/vitte/editors" \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "editor integrations directory is empty for $family $release $arch"

  find "$root/usr/local/share/vitte/completions" \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "shell completions directory is empty for $family $release $arch"

  find "$root/usr/local/share/vitte" \
    -type f \
    -print \
    -quit |
    grep -q . ||
    die "Vitte share payload is empty for $family $release $arch"
}

write_install_script() {
  stage=$1

  cat > "$stage/install.sh" <<'SH'
#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}

case "$PREFIX" in
  /) printf 'PREFIX must not be /\n' >&2; exit 1;;
  /*) ;;
  *)
    printf 'PREFIX must be absolute: %s\n' "$PREFIX" >&2
    exit 1
    ;;
esac

case "$PREFIX" in */) PREFIX=${PREFIX%/};; esac
case "$PREFIX/" in
  */../*|*/./*|*//*)
    printf 'PREFIX must not contain dot or empty path components: %s\n' "$PREFIX" >&2
    exit 1
    ;;
esac

if [ -n "$DESTDIR" ]; then
  case "$DESTDIR" in
    /) printf 'DESTDIR must not be /\n' >&2; exit 1;;
    /*) ;;
    *) printf 'DESTDIR must be absolute: %s\n' "$DESTDIR" >&2; exit 1;;
  esac
  case "$DESTDIR" in */) DESTDIR=${DESTDIR%/};; esac
  case "$DESTDIR/" in
    */../*|*/./*|*//*)
      printf 'DESTDIR must not contain dot or empty path components: %s\n' "$DESTDIR" >&2
      exit 1
      ;;
  esac
fi

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

[ -d "$HERE/root" ] || {
  printf 'Vitte installer payload is missing: %s/root\n' "$HERE" >&2
  exit 1
}

target_root=${DESTDIR:-/}
target_prefix=$target_root$PREFIX

check_path() {
  current=$1
  while :; do
    [ ! -L "$current" ] || {
      printf 'refusing to follow a symbolic link: %s\n' "$current" >&2
      exit 1
    }
    [ "$current" != / ] || break
    parent=$(dirname "$current")
    [ "$parent" != "$current" ] || break
    current=$parent
  done
}

check_path "$target_prefix"
mkdir -p "$target_prefix" 2>/dev/null || {
  printf 'cannot create installation prefix: %s\n' "$target_prefix" >&2
  exit 1
}
if [ -z "$DESTDIR" ] &&
   [ "$(id -u)" -ne 0 ] &&
   [ ! -w "$target_prefix" ]
then
  printf 'Vitte installation requires root or a writable PREFIX\n' >&2
  exit 1
fi

if [ "$PREFIX" = /usr/local ]; then
  cp -pR "$HERE/root/." "$target_root/"
else
  [ -d "$HERE/root/usr/local" ] || {
    printf 'Vitte payload is missing usr/local\n' >&2
    exit 1
  }

  cp -pR "$HERE/root/usr/local/." "$target_prefix/"
fi

integration_helper=$target_prefix/share/vitte/tools/install-unix-integrations.sh
[ -x "$integration_helper" ] || {
  printf 'Vitte integration installer is missing: %s\n' "$integration_helper" >&2
  exit 1
}
PREFIX=$PREFIX DESTDIR=$DESTDIR "$integration_helper"

printf 'Vitte installed in %s%s.\n' \
  "$DESTDIR" \
  "$PREFIX"

printf 'Run: %s/bin/vitte --help\n' \
  "$PREFIX"

printf 'Editor integrations: %s/share/vitte/editors\n' \
  "$PREFIX"

printf 'Shell completions: %s/share/vitte/completions\n' \
  "$PREFIX"
SH

  chmod 0755 "$stage/install.sh"

  cat > "$stage/uninstall.sh" <<'SH'
#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}

case "$PREFIX" in
  /) printf 'PREFIX must not be /\n' >&2; exit 1;;
  /*) ;;
  *)
    printf 'PREFIX must be absolute: %s\n' "$PREFIX" >&2
    exit 1
    ;;
esac

case "$PREFIX" in */) PREFIX=${PREFIX%/};; esac
case "$PREFIX/" in
  */../*|*/./*|*//*)
    printf 'PREFIX must not contain dot or empty path components: %s\n' "$PREFIX" >&2
    exit 1
    ;;
esac

if [ -n "$DESTDIR" ]; then
  case "$DESTDIR" in
    /) printf 'DESTDIR must not be /\n' >&2; exit 1;;
    /*) ;;
    *) printf 'DESTDIR must be absolute: %s\n' "$DESTDIR" >&2; exit 1;;
  esac
  case "$DESTDIR" in */) DESTDIR=${DESTDIR%/};; esac
  case "$DESTDIR/" in
    */../*|*/./*|*//*)
      printf 'DESTDIR must not contain dot or empty path components: %s\n' "$DESTDIR" >&2
      exit 1
      ;;
  esac
fi

root=${DESTDIR:-/}
target_prefix=$root$PREFIX

check_path() {
  current=$1
  while :; do
    [ ! -L "$current" ] || {
      printf 'refusing to follow a symbolic link: %s\n' "$current" >&2
      exit 1
    }
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
if [ -z "$DESTDIR" ] &&
   [ "$(id -u)" -ne 0 ] &&
   [ ! -w "$target_prefix" ]
then
  printf 'Vitte uninstall requires root or a writable PREFIX\n' >&2
  exit 1
fi

rm -f \
  "$target_prefix/bin/vitte" \
  "$target_prefix/bin/vittec" \
  "$target_prefix/bin/vitte-installer-doctor"

integration_helper=$target_prefix/share/vitte/tools/install-unix-integrations.sh
if [ -x "$integration_helper" ]; then
  PREFIX=$PREFIX DESTDIR=$DESTDIR "$integration_helper" uninstall
fi

rm -rf \
  "$target_prefix/libexec/vitte" \
  "$target_prefix/share/vitte"

printf 'Vitte removed from %s%s.\n' \
  "$DESTDIR" \
  "$PREFIX"
SH

  chmod 0755 "$stage/uninstall.sh"
}

write_install_documentation() {
  stage=$1
  family=$2
  release=$3
  arch=$4

  cat > "$stage/INSTALL.txt" <<EOF
Vitte $VERSION complete installer for $family $release $arch

INSTALLATION

Run:

    ./install.sh

The default installation prefix is:

    /usr/local

A system-wide installation may require root privileges.

CUSTOM PREFIX

Example:

    PREFIX=/opt/vitte ./install.sh

STAGED INSTALLATION

Example:

    DESTDIR=/tmp/vitte-root ./install.sh

INSTALLED COMPONENTS

    - Vitte compiler commands
    - Vitte runtime
    - Vitte standard library
    - Vitte sources
    - Documentation and manual pages
    - Examples
    - Editor integrations
    - Shell completions
    - Locales
    - Assets
    - Installer diagnostics

EDITOR INTEGRATIONS

Installed in:

    /usr/local/share/vitte/editors

SHELL COMPLETIONS

Installed in:

    /usr/local/share/vitte/completions

VALIDATION

After installation:

    /usr/local/bin/vitte --version
    /usr/local/bin/vitte --help

If available:

    /usr/local/bin/vitte-installer-doctor

UNINSTALLATION

Run:

    ./uninstall.sh
EOF
}

normalize_arch() {
  case "$1" in
    x86_64 | X86_64 | amd64 | AMD64)
      printf '%s\n' amd64
      ;;

    i386 | i486 | i586 | i686 | x86)
      printf '%s\n' i386
      ;;

    aarch64 | AArch64 | AARCH64 | arm64 | ARM64)
      printf '%s\n' arm64
      ;;

    armv7 | armv7l | armhf)
      printf '%s\n' armv7
      ;;

    armv6 | armel)
      printf '%s\n' armv6
      ;;

    riscv64 | RISC-V64 | RISCV64)
      printf '%s\n' riscv64
      ;;

    powerpc | ppc)
      printf '%s\n' powerpc
      ;;

    powerpc64 | ppc64)
      printf '%s\n' powerpc64
      ;;

    powerpc64le | ppc64le)
      printf '%s\n' powerpc64le
      ;;

    sparc64)
      printf '%s\n' sparc64
      ;;

    mips | mipseb)
      printf '%s\n' mips
      ;;

    mipsel)
      printf '%s\n' mipsel
      ;;

    mips64 | mips64eb)
      printf '%s\n' mips64
      ;;

    mips64el)
      printf '%s\n' mips64el
      ;;

    *)
      die "unsupported BSD architecture: $1"
      ;;
  esac
}

bsd_arches_for_family() {
  case "$1" in
    freebsd | hardenedbsd)
      printf '%s\n' \
        'amd64 i386 arm64 armv7 armv6 riscv64 powerpc powerpc64 powerpc64le'
      ;;

    openbsd)
      printf '%s\n' \
        'amd64 i386 arm64 armv7 riscv64 sparc64 powerpc64'
      ;;

    netbsd)
      printf '%s\n' \
        'amd64 i386 arm64 armv7 armv6 riscv64 sparc64 powerpc powerpc64 mips mipsel mips64 mips64el'
      ;;

    dragonfly)
      printf '%s\n' amd64
      ;;

    midnightbsd)
      printf '%s\n' 'amd64 i386'
      ;;

    ghostbsd | nomadbsd | hellosystem)
      printf '%s\n' amd64
      ;;

    *)
      die "unsupported BSD family: $1"
      ;;
  esac
}

bsd_releases_for_family() {
  case "$1" in
    freebsd)
      printf '%s\n' '12 13 14 15'
      ;;

    openbsd)
      printf '%s\n' '7.4 7.5 7.6 7.7 7.8'
      ;;

    netbsd)
      printf '%s\n' '9 10'
      ;;

    dragonfly)
      printf '%s\n' '6.4'
      ;;

    midnightbsd)
      printf '%s\n' '3.0'
      ;;

    ghostbsd)
      printf '%s\n' '24'
      ;;

    hardenedbsd)
      printf '%s\n' '13 14'
      ;;

    nomadbsd)
      printf '%s\n' '141'
      ;;

    hellosystem)
      printf '%s\n' '0.8'
      ;;

    *)
      die "unsupported BSD family: $1"
      ;;
  esac
}

normalize_family() {
  case "$1" in
    helloSystem | hellosystem)
      printf '%s\n' hellosystem
      ;;

    freebsd | openbsd | netbsd | dragonfly | midnightbsd | ghostbsd | hardenedbsd | nomadbsd)
      printf '%s\n' "$1"
      ;;

    *)
      die "unsupported BSD family: $1"
      ;;
  esac
}

release_supported() {
  family=$1
  requested=$2

  for supported in $(bsd_releases_for_family "$family"); do
    if [ "$supported" = "$requested" ]; then
      return 0
    fi
  done

  return 1
}

arch_supported() {
  family=$1
  requested=$2

  for supported in $(bsd_arches_for_family "$family"); do
    if [ "$supported" = "$requested" ]; then
      return 0
    fi
  done

  return 1
}

build_one() {
  family=$1
  release=$2
  arch=$3

  stage=$ROOT_DIR/target/installer-$family-$release-$arch-portable
  root=$stage/root

  archive=$OUT_DIR/vitte-$VERSION-$family-$release-$arch-installer.tar.xz
  checksum=$archive.sha256

  printf '[build-bsd-installers] building %s %s %s\n' \
    "$family" \
    "$release" \
    "$arch"

  rm -rf "$stage"

  mkdir -p \
    "$stage" \
    "$OUT_DIR"

  VERSION=$VERSION \
  BSD_RELEASE=$release \
    "$ROOT_DIR/scripts/stage-installer-payload.sh" \
    "$root" \
    "$family" \
    "$arch" \
    unix

  add_integrations "$root"

  verify_payload \
    "$root" \
    "$family" \
    "$release" \
    "$arch"

  write_install_script "$stage"

  write_install_documentation \
    "$stage" \
    "$family" \
    "$release" \
    "$arch"

  rm -f \
    "$archive" \
    "$checksum"

  scripts_build_tar_xz \
    "$archive" \
    "$stage" \
    install.sh \
    uninstall.sh \
    INSTALL.txt \
    root

  scripts_build_sha256_write \
    "$archive" \
    "$checksum"

  scripts_build_sha256_check \
    "$archive" \
    "$checksum"

  scripts_build_write_artifact_manifest \
    "$archive" \
    "$family" \
    "$arch" \
    "$VERSION"

  printf '[build-bsd-installers] wrote %s\n' \
    "$archive"

  printf '[build-bsd-installers] wrote %s\n' \
    "$checksum"
}

scripts_build_require tar
scripts_build_require find
scripts_build_require grep
scripts_build_require sed

[ -x "$ROOT_DIR/scripts/stage-installer-payload.sh" ] ||
  die "payload staging script is missing or not executable"

[ -d "$EDITORS_DIR" ] ||
  die "editors directory not found: $EDITORS_DIR"

[ -d "$COMPLETIONS_DIR" ] ||
  die "completions directory not found: $COMPLETIONS_DIR"

case "$BSD_FAMILY" in
  all)
    families='freebsd openbsd netbsd dragonfly midnightbsd ghostbsd hardenedbsd nomadbsd hellosystem'
    ;;

  *)
    families=$(normalize_family "$BSD_FAMILY")
    ;;
esac

case "$ARCH" in
  all)
    requested_arches=all
    ;;

  *)
    requested_arches=$(normalize_arch "$ARCH")
    ;;
esac

if [ "$LIST_TARGETS" -eq 1 ]; then
  for family in $families; do
    releases=$(bsd_releases_for_family "$family")
    arches=$(bsd_arches_for_family "$family")

    for release in $releases; do
      for arch in $arches; do
        printf '%s %s %s\n' \
          "$family" \
          "$release" \
          "$arch"
      done
    done
  done

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

for family in $families; do
  if [ "$requested_arches" = all ]; then
    arches=$(bsd_arches_for_family "$family")
  else
    if ! arch_supported "$family" "$requested_arches"; then
      die "architecture $requested_arches is not supported for $family"
    fi

    arches=$requested_arches
  fi

  case "$BSD_RELEASE" in
    all)
      releases=$(bsd_releases_for_family "$family")
      ;;

    *)
      if ! release_supported "$family" "$BSD_RELEASE"; then
        die "release $BSD_RELEASE is not supported for $family"
      fi

      releases=$BSD_RELEASE
      ;;
  esac

  for release in $releases; do
    for arch in $arches; do
      build_one \
        "$family" \
        "$release" \
        "$arch"
    done
  done
done

printf \
  '[build-bsd-installers] all BSD installers completed family=%s release=%s arch=%s\n' \
  "$BSD_FAMILY" \
  "$BSD_RELEASE" \
  "$ARCH"
