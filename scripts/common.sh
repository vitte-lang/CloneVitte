#!/bin/sh

scripts_build_die() {
  script_name=${SCRIPT_NAME:-scripts_build}
  printf '[%s][error] %s\n' "$script_name" "$*" >&2
  exit 1
}

scripts_build_log() {
  script_name=${SCRIPT_NAME:-scripts_build}
  printf '[%s] %s\n' "$script_name" "$*"
}

scripts_build_require() {
  command -v "$1" >/dev/null 2>&1 ||
    scripts_build_die "missing required tool: $1"
}

scripts_build_require_file() {
  required_file=$1
  description=${2:-file}

  [ -f "$required_file" ] ||
    scripts_build_die "$description not found: $required_file"

  [ -s "$required_file" ] ||
    scripts_build_die "$description is empty: $required_file"
}

scripts_build_require_executable() {
  file=$1
  description=${2:-executable}

  [ -f "$file" ] ||
    scripts_build_die "$description not found: $file"

  [ -x "$file" ] ||
    scripts_build_die "$description is not executable: $file"
}

scripts_build_root_dir() {
  if [ -n "${ROOT_DIR:-}" ]; then
    printf '%s\n' "$ROOT_DIR"
    return 0
  fi

  CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd
}

scripts_build_package_version() {
  root_dir=$(scripts_build_root_dir)

  for candidate in \
    "$root_dir/VERSION" \
    "$root_dir/toolchain/scripts/package/PACKAGE_VERSION"
  do
    if [ -f "$candidate" ] && [ -s "$candidate" ]; then
      version=$(tr -d ' \t\r\n' < "$candidate")
      if [ -n "$version" ]; then
        printf '%s\n' "$version"
        return 0
      fi
    fi
  done

  if [ -f "$root_dir/README.md" ]; then
    version=$(
      sed -n \
        's/.*version-\([0-9][0-9A-Za-z._+-]*\)-.*/\1/p' \
        "$root_dir/README.md" |
      sed -n '1p'
    )

    if [ -n "$version" ]; then
      printf '%s\n' "$version"
      return 0
    fi
  fi

  printf '%s\n' '0.1.1'
}

scripts_build_absolute_path() {
  path=${1:-}

  [ -n "$path" ] || return 1

  case "$path" in
    /*)
      directory=$(dirname "$path")
      name=$(basename "$path")

      if [ -d "$directory" ]; then
        directory=$(CDPATH= cd -- "$directory" 2>/dev/null && pwd) ||
          return 1
        printf '%s/%s\n' "$directory" "$name"
        return 0
      fi

      printf '%s\n' "$path"
      ;;

    */*)
      directory=${path%/*}
      name=${path##*/}

      directory=$(CDPATH= cd -- "$directory" 2>/dev/null && pwd) ||
        return 1

      printf '%s/%s\n' "$directory" "$name"
      ;;

    *)
      found=$(command -v "$path" 2>/dev/null || true)

      [ -n "$found" ] || return 1

      case "$found" in
        /*)
          printf '%s\n' "$found"
          ;;
        *)
          scripts_build_absolute_path "$found"
          ;;
      esac
      ;;
  esac
}

scripts_build_verify_release_manifest() {
  root_dir=$(scripts_build_root_dir)
  binary=${1:-$root_dir/build/bin/vitte}

  scripts_build_require_executable \
    "$binary" \
    "native C compiler"

  "$binary" --version >/dev/null 2>&1 ||
    scripts_build_die \
      "native C compiler failed its version check: $binary"
}

scripts_build_macos_binary() {
  arch=$1
  root_dir=$(scripts_build_root_dir)

  case "$arch" in
    arm64 | aarch64 | AArch64 | AARCH64)
      candidate=$root_dir/target/macos-arm64/vitte
      ;;

    x86_64 | amd64 | AMD64 | X86_64)
      candidate=$root_dir/target/macos-x86_64/vitte
      ;;

    universal | universal2)
      candidate=$root_dir/target/universal/vitte
      ;;

    i386 | x86 | macos2006-i386)
      candidate=$root_dir/target/macos2006-i386/vitte
      ;;

    *)
      scripts_build_die \
        "unsupported macOS architecture: $arch"
      ;;
  esac

  scripts_build_require_executable \
    "$candidate" \
    "Vitte macOS $arch binary"

  scripts_build_absolute_path "$candidate"
}

detect_vitte() {
  root_dir=$(scripts_build_root_dir)

  vitte_root=${VITTE_ROOT:-}
  vitte_prefix=

  if [ -n "$vitte_root" ]; then
    case "$vitte_root" in
      */share/vitte)
        vitte_prefix=${vitte_root%/share/vitte}
        ;;
      *)
        vitte_prefix=$vitte_root
        ;;
    esac
  fi

  for candidate in \
    "${VITTE_BIN:-}" \
    "${vitte_root:+$vitte_root/bin/vitte}" \
    "${vitte_prefix:+$vitte_prefix/bin/vitte}" \
    "$root_dir/build/bin/vitte" \
    "$root_dir/target/macos-arm64/vitte" \
    "$root_dir/target/macos-x86_64/vitte" \
    "$root_dir/target/universal/vitte" \
    "$PWD/bin/vitte" \
    "$root_dir/bin/vitte" \
    "/opt/homebrew/bin/vitte" \
    "/usr/local/bin/vitte" \
    "/usr/bin/vitte"
  do
    [ -n "$candidate" ] || continue

    if [ -x "$candidate" ]; then
      scripts_build_absolute_path "$candidate"
      return 0
    fi
  done

  return 1
}

install_vitte_if_missing() {
  detected=$(detect_vitte 2>/dev/null || true)

  if [ -n "$detected" ]; then
    scripts_build_verify_release_manifest "$detected"

    VITTE_ABSOLUTE=$detected
    export VITTE_ABSOLUTE

    printf '%s\n' "$detected"
    return 0
  fi

  scripts_build_die \
    "cannot find Vitte; run make, or set VITTE_BIN explicitly"
}

verify_vitte() {
  vitte=${1:-${VITTE_ABSOLUTE:-}}

  if [ -z "$vitte" ]; then
    vitte=$(install_vitte_if_missing)
  fi

  vitte=$(scripts_build_absolute_path "$vitte") ||
    scripts_build_die "cannot resolve Vitte path: $vitte"

  scripts_build_require_executable \
    "$vitte" \
    "Vitte compiler"

  "$vitte" --version >/dev/null 2>&1 ||
    scripts_build_die \
      "Vitte version check failed: $vitte --version"

  "$vitte" --help >/dev/null 2>&1 ||
    scripts_build_die \
      "Vitte help check failed: $vitte --help"

  smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/vitte-script-build-verify.XXXXXX") ||
    scripts_build_die "cannot create a temporary smoke directory"

  cleanup_smoke() {
    /bin/rm -rf -- "$smoke_dir"
  }

  trap cleanup_smoke EXIT HUP INT TERM

  {
    printf '%s\n' 'proc main() -> int {'
    printf '%s\n' '    return 0;'
    printf '%s\n' '}'
  } > "$smoke_dir/main.vit"

  (
    cd "$smoke_dir"
    "$vitte" check main.vit >/dev/null 2>&1
  ) ||
    scripts_build_die \
      "Vitte post-install check failed: $vitte check main.vit"

  case "$(uname -s 2>/dev/null || printf '%s' unknown)" in
    MINGW* | MSYS* | CYGWIN*)
      (
        cd "$smoke_dir"
        "$vitte" compile main.vit --emit-c -o main.c >/dev/null 2>&1 &&
          cc -std=c17 main.c -o main.exe &&
          ./main.exe
      ) ||
        scripts_build_die \
          "Vitte Windows C-emission smoke failed: $vitte compile main.vit --emit-c"
      ;;

    *)
      (
        cd "$smoke_dir"
        "$vitte" compile main.vit -o main >/dev/null 2>&1
      ) ||
        scripts_build_die \
          "Vitte post-install compile failed: $vitte compile main.vit -o main"

      smoke_program=$smoke_dir/main
      [ -x "$smoke_program" ] || smoke_program=$smoke_dir/main.exe
      [ -x "$smoke_program" ] ||
        scripts_build_die "Vitte post-install executable is missing: $smoke_dir/main"
      "$smoke_program" >/dev/null 2>&1 ||
        scripts_build_die "Vitte post-install executable failed: $smoke_program"
      ;;
  esac

  cleanup_smoke
  trap - EXIT HUP INT TERM

  VITTE_ABSOLUTE=$vitte
  export VITTE_ABSOLUTE

  printf '%s\n' "$vitte"
}

run_vitte_absolute() {
  vitte=${VITTE_ABSOLUTE:-}

  if [ -z "$vitte" ] || [ ! -x "$vitte" ]; then
    vitte=$(install_vitte_if_missing)
  fi

  vitte=$(scripts_build_absolute_path "$vitte") ||
    scripts_build_die "cannot resolve Vitte executable"

  "$vitte" "$@"
}

scripts_build_parse_common_flags() {
  DRY_RUN=${DRY_RUN:-0}
  HELP=${HELP:-0}
  LIST_TARGETS=${LIST_TARGETS:-0}
  PRINT_ENV=${PRINT_ENV:-0}
  VERIFY_ONLY=${VERIFY_ONLY:-}
  CLEAN=${CLEAN:-0}

  while [ "$#" -gt 0 ]; do
    case "$1" in
      --dry-run)
        DRY_RUN=1
        ;;

      --help | -h)
        HELP=1
        ;;

      --list-targets)
        LIST_TARGETS=1
        ;;

      --print-env)
        PRINT_ENV=1
        ;;

      --verify-only)
        shift
        [ "$#" -gt 0 ] ||
          scripts_build_die \
            "missing OUT_DIR for --verify-only"
        VERIFY_ONLY=$1
        ;;

      --clean)
        CLEAN=1
        ;;

      --)
        shift
        break
        ;;

      *)
        scripts_build_die \
          "unsupported option: $1"
        ;;
    esac

    shift
  done

  export \
    DRY_RUN \
    HELP \
    LIST_TARGETS \
    PRINT_ENV \
    VERIFY_ONLY \
    CLEAN
}

scripts_build_maybe_help() {
  usage=$1

  if [ "${HELP:-0}" -eq 1 ]; then
    printf '%s\n' "$usage"
    exit 0
  fi
}

scripts_build_maybe_dry_run() {
  description=$1

  if [ "${DRY_RUN:-0}" -eq 1 ]; then
    script_name=${SCRIPT_NAME:-scripts_build}
    printf '[%s][dry-run] %s\n' \
      "$script_name" \
      "$description"
    exit 0
  fi
}

scripts_build_sha256_write() {
  file=$1
  output=${2:-$file.sha256}

  scripts_build_require_file \
    "$file" \
    "checksum input"

  output_dir=$(dirname "$output")
  output_name=$(basename "$output")
  file_dir=$(dirname "$file")
  file_name=$(basename "$file")

  mkdir -p "$output_dir"

  if [ "${SCRIPTS_BUILD_SHA256_BACKEND:-auto}" != python ] &&
     command -v shasum >/dev/null 2>&1
  then
    digest=$(
      shasum -a 256 "$file" |
      awk '{print $1}'
    )

    printf '%s  %s\n' \
      "$digest" \
      "$file_name" > "$output"

    return 0
  fi

  if [ "${SCRIPTS_BUILD_SHA256_BACKEND:-auto}" != python ] &&
     command -v sha256sum >/dev/null 2>&1
  then
    digest=$(
      sha256sum "$file" |
      awk '{print $1}'
    )

    printf '%s  %s\n' \
      "$digest" \
      "$file_name" > "$output"

    return 0
  fi

  scripts_build_require python3

  python3 - "$file" "$output" <<'PY'
import hashlib
import sys
from pathlib import Path

file = Path(sys.argv[1])
output = Path(sys.argv[2])

digest = hashlib.sha256(file.read_bytes()).hexdigest()

output.write_text(
    f"{digest}  {file.name}\n",
    encoding="utf-8",
)
PY

  [ -s "$output" ] ||
    scripts_build_die \
      "failed to create checksum: $output"

  : "$output_name"
  : "$file_dir"
}

scripts_build_sha256_check() {
  file=$1
  sum_file=${2:-$file.sha256}

  scripts_build_require_file \
    "$file" \
    "artifact"

  scripts_build_require_file \
    "$sum_file" \
    "checksum"

  expected=$(
    awk 'NR == 1 { print $1 }' "$sum_file"
  )

  [ -n "$expected" ] ||
    scripts_build_die \
      "invalid checksum file: $sum_file"

  if [ "${SCRIPTS_BUILD_SHA256_BACKEND:-auto}" != python ] &&
     command -v shasum >/dev/null 2>&1
  then
    actual=$(
      shasum -a 256 "$file" |
      awk '{print $1}'
    )
  elif [ "${SCRIPTS_BUILD_SHA256_BACKEND:-auto}" != python ] &&
       command -v sha256sum >/dev/null 2>&1
  then
    actual=$(
      sha256sum "$file" |
      awk '{print $1}'
    )
  else
    scripts_build_require python3

    actual=$(
      python3 - "$file" <<'PY'
import hashlib
import sys
from pathlib import Path

file = Path(sys.argv[1])
print(hashlib.sha256(file.read_bytes()).hexdigest())
PY
    )
  fi

  [ "$actual" = "$expected" ] ||
    scripts_build_die \
      "checksum mismatch: $file"
}

scripts_build_tar_list_xz() {
  archive=$1

  scripts_build_require_file \
    "$archive" \
    "archive"

  if tar -tJf "$archive" >/dev/null 2>&1; then
    tar -tJf "$archive"
    return 0
  fi

  if tar -tzf "$archive" >/dev/null 2>&1; then
    tar -tzf "$archive"
    return 0
  fi

  scripts_build_die \
    "unsupported or invalid archive: $archive"
}

scripts_build_copy_tree() {
  source=$1
  destination=$2

  [ -e "$source" ] || return 0

  mkdir -p "$destination"

  COPYFILE_DISABLE=1 \
    tar -cf - -C "$source" . |
    tar -xf - -C "$destination"

  find "$destination" \
    \( \
      -name '.DS_Store' \
      -o -name '._*' \
      -o -name '.vitte-cache' \
      -o -name 'build' \
      -o -name '__pycache__' \
      -o -name 'node_modules' \
    \) \
    -prune \
    -exec rm -rf {} \; \
    2>/dev/null ||
    true
}

scripts_build_install_modules() {
  source_root=$1
  share_root=$2
  modules_root=$source_root/modules
  destination=${3:-$share_root/modules}

  [ -d "$modules_root" ] ||
    scripts_build_die "modules directory not found: $modules_root"

  module_directory_found=0
  for module in "$modules_root"/*; do
    if [ -d "$module" ]; then
      module_directory_found=1
      break
    fi
  done

  [ "$module_directory_found" -eq 1 ] ||
    scripts_build_die "modules directory is empty: $modules_root"

  mkdir -p "$destination"
  scripts_build_copy_tree "$modules_root" "$destination"

  module_count=0
  for module in "$modules_root"/*; do
    [ -d "$module" ] || continue

    module_name=$(basename "$module")
    [ -f "$module/package.toml" ] ||
      scripts_build_die \
        "module $module_name is missing package.toml: $module/package.toml"

    find "$module" \
      -type f \
      -name '*.vit' \
      -print \
      -quit |
      grep -q . ||
      scripts_build_die "module $module_name contains no Vitte sources: $module"

    [ -f "$destination/$module_name/package.toml" ] ||
      scripts_build_die \
        "module $module_name was not installed: $destination/$module_name/package.toml"

    module_count=$((module_count + 1))
  done

  [ "$module_count" -gt 0 ] ||
    scripts_build_die "no installable modules found in: $modules_root"

  scripts_build_log \
    "installed $module_count module(s) in $destination"
}

scripts_build_verify_modules() {
  share_root=$1
  modules_root=${2:-$share_root/modules}

  [ -d "$modules_root" ] ||
    scripts_build_die "installed modules directory missing: $modules_root"

  module_count=0
  for package in "$modules_root"/*/package.toml; do
    [ -f "$package" ] || continue
    module_dir=$(dirname "$package")
    find "$module_dir" \
      -type f \
      -name '*.vit' \
      -print \
      -quit |
      grep -q . ||
      scripts_build_die "installed module has no Vitte sources: $module_dir"
    module_count=$((module_count + 1))
  done

  [ "$module_count" -gt 0 ] ||
    scripts_build_die "installed modules directory is empty: $modules_root"
}

scripts_build_tar_gz() {
  output=$1
  base=$2
  shift 2

  [ -d "$base" ] ||
    scripts_build_die \
      "archive base directory not found: $base"

  mkdir -p "$(dirname "$output")"

  COPYFILE_DISABLE=1 \
    tar -czf "$output" -C "$base" "$@"

  scripts_build_require_file \
    "$output" \
    "gzip archive"
}

scripts_build_tar_xz() {
  output=$1
  base=$2
  shift 2

  [ -d "$base" ] ||
    scripts_build_die \
      "archive base directory not found: $base"

  mkdir -p "$(dirname "$output")"

  COPYFILE_DISABLE=1 \
    tar -cJf "$output" -C "$base" "$@"

  scripts_build_require_file \
    "$output" \
    "xz archive"
}

scripts_build_detect_libc() {
  kernel=$(uname -s 2>/dev/null || printf '%s\n' unknown)

  case "$kernel" in
    Linux)
      if command -v ldd >/dev/null 2>&1; then
        ldd_output=$(
          ldd --version 2>&1 |
          sed -n '1,3p' ||
          true
        )

        if printf '%s\n' "$ldd_output" |
           grep -qi musl
        then
          printf '%s\n' musl
          return 0
        fi

        if printf '%s\n' "$ldd_output" |
           grep -Eqi 'glibc|GNU libc|GNU C Library'
        then
          printf '%s\n' glibc
          return 0
        fi
      fi

      for loader in \
        /lib/ld-musl-* \
        /usr/lib/ld-musl-*
      do
        if [ -e "$loader" ]; then
          printf '%s\n' musl
          return 0
        fi
      done

      printf '%s\n' unknown-linux-libc
      ;;

    FreeBSD | OpenBSD | NetBSD | DragonFly)
      printf '%s\n' bsd-libc
      ;;

    SunOS)
      printf '%s\n' solaris-libc
      ;;

    Darwin)
      printf '%s\n' libSystem
      ;;

    *)
      printf '%s\n' unknown-libc
      ;;
  esac
}

scripts_build_libc_for_target() {
  platform=$1

  case "$platform" in
    linux)
      printf '%s\n' glibc-or-musl
      ;;

    freebsd | openbsd | netbsd | dragonfly | midnightbsd | ghostbsd | hardenedbsd | nomadbsd | hellosystem)
      printf '%s\n' bsd-libc
      ;;

    solaris)
      printf '%s\n' solaris-libc
      ;;

    macos)
      printf '%s\n' libSystem
      ;;

    windows)
      printf '%s\n' msvcrt-compatible
      ;;

    *)
      printf '%s\n' unknown-libc
      ;;
  esac
}

scripts_build_minimum_version_for_target() {
  platform=$1

  case "$platform" in
    linux)
      printf '%s\n' 'Linux 3.2, glibc 2.17 or musl 1.2'
      ;;

    freebsd)
      printf '%s\n' 'FreeBSD 13'
      ;;

    openbsd)
      printf '%s\n' 'OpenBSD 7.0'
      ;;

    netbsd)
      printf '%s\n' 'NetBSD 9'
      ;;

    dragonfly)
      printf '%s\n' 'DragonFly BSD 6'
      ;;

    midnightbsd)
      printf '%s\n' 'MidnightBSD 3'
      ;;

    ghostbsd)
      printf '%s\n' 'GhostBSD 24'
      ;;

    hardenedbsd)
      printf '%s\n' 'HardenedBSD 13'
      ;;

    nomadbsd)
      printf '%s\n' 'NomadBSD 1.4'
      ;;

    hellosystem)
      printf '%s\n' 'helloSystem 0.8'
      ;;

    solaris)
      printf '%s\n' 'Solaris 10'
      ;;

    macos)
      printf '%s\n' 'macOS 10.13'
      ;;

    windows)
      printf '%s\n' 'Windows XP SP3'
      ;;

    *)
      printf '%s\n' unknown
      ;;
  esac
}

scripts_build_static_build_supported() {
  platform=$1
  arch=$2

  case "$platform:$arch" in
    linux:amd64 | \
    linux:x86_64 | \
    linux:i386 | \
    linux:arm64 | \
    linux:aarch64 | \
    linux:armv7 | \
    linux:armhf | \
    linux:armv6 | \
    linux:armel | \
    linux:riscv64)
      return 0
      ;;

    *)
      return 1
      ;;
  esac
}

scripts_build_abi_label() {
  platform=$1
  arch=$2
  libc=${3:-$(scripts_build_libc_for_target "$platform")}

  printf '%s-%s-%s\n' \
    "$platform" \
    "$arch" \
    "$libc"
}

scripts_build_write_artifact_manifest() {
  file=$1
  platform=$2
  arch=$3
  version=$4
  output=${5:-$file.MANIFEST.json}

  scripts_build_require_file \
    "$file" \
    "artifact"

  libc=$(scripts_build_libc_for_target "$platform")
  minimum_version=$(scripts_build_minimum_version_for_target "$platform")
  if [ -n "${6:-}" ]; then
    minimum_version=$6
  fi
  abi=$(scripts_build_abi_label "$platform" "$arch" "$libc")

  static_when_possible=false

  if scripts_build_static_build_supported "$platform" "$arch"; then
    static_when_possible=true
  fi

  mkdir -p "$(dirname "$output")"

  scripts_build_require python3

  python3 - \
    "$file" \
    "$platform" \
    "$arch" \
    "$version" \
    "$output" \
    "$libc" \
    "$minimum_version" \
    "$abi" \
    "$static_when_possible" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

file = Path(sys.argv[1])

manifest = {
    "schema": "org.vitte.installer-artifact.v1",
    "name": file.name,
    "platform": sys.argv[2],
    "os": sys.argv[2],
    "arch": sys.argv[3],
    "version": sys.argv[4],
    "libc": sys.argv[6],
    "minimum_version": sys.argv[7],
    "abi": sys.argv[8],
    "static_when_possible": sys.argv[9] == "true",
    "installed_commands": [
        "vitte",
        "vittec",
        "vitte-installer-doctor",
    ],
    "contents": [
        "compiler",
        "runtime",
        "stdlib",
        "sources",
        "documentation",
        "examples",
        "editors",
        "system-completions",
        "locales",
        "assets",
        "compiled-stdlib",
        "compiled-packages",
        "local-package-registry",
        "checksums",
        "sbom",
        "installer-doctor",
    ],
    "size": file.stat().st_size,
    "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
}

Path(sys.argv[5]).write_text(
    json.dumps(
        manifest,
        indent=2,
        sort_keys=True,
    ) + "\n",
    encoding="utf-8",
)
PY

  scripts_build_require_file \
    "$output" \
    "artifact manifest"
}
