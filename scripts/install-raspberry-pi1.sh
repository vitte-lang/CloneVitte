#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SCRIPT_NAME=install-raspberry-pi1
. "$ROOT_DIR/scripts/common.sh"

PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
PI1_JOBS=${PI1_JOBS:-1}
DRY_RUN=${DRY_RUN:-0}
ALLOW_NON_PI=${ALLOW_NON_PI:-0}
FULL=${FULL:-0}
BOOTSTRAP=${BOOTSTRAP:-0}
VERIFY=${VERIFY:-1}
VERIFY_ONLY=${VERIFY_ONLY:-0}
OFFLINE=${OFFLINE:-1}
BENCHMARK=${BENCHMARK:-0}
PRINT_ENV=${PRINT_ENV:-0}
BACKUP_DIR=${BACKUP_DIR:-}
LOG_FILE=${LOG_FILE:-}
MIN_AVAILABLE_KIB=${MIN_AVAILABLE_KIB:-65536}

usage() {
  cat <<'HELP'
usage: install-raspberry-pi1.sh [options]
  --prefix DIR          install prefix (default /usr/local)
  --destdir DIR         stage the install below DIR; never changes the host
  --jobs N              native build jobs (default 1)
  --full                include runtime, modules, headers and editor assets
  --bootstrap           run BOOTSTRAP_CMD before the native build
  --verify              check, compile and run a smoke program (default)
  --no-verify           skip the smoke program
  --verify-only         verify an existing live installation
  --offline             perform no network operations (default)
  --benchmark           report elapsed build and install time
  --backup-dir DIR      store replaced files in a new directory below DIR
  --log-file FILE       append installer output to FILE
  --allow-non-pi        permit host testing only when --destdir is set
  --min-memory-kib N    warn when available memory is below N KiB
  --dry-run             show the plan without building or installing
  --print-env           show effective configuration
  --help

Live installs preserve replaced files in a timestamped backup directory and
roll back files changed by this installer if the commit fails.
The installer builds natively. --allow-non-pi does not cross-compile.
HELP
}

fail() { printf '[%s][error] %s\n' "$SCRIPT_NAME" "$*" >&2; exit 1; }
log() { printf '[%s] %s\n' "$SCRIPT_NAME" "$*"; }
require_tool() { command -v "$1" >/dev/null 2>&1 || fail "missing tool: $1"; }
positive() {
  case "$2" in ''|*[!0-9]*) fail "$1 must be a positive integer";; esac
  [ "$2" -gt 0 ] || fail "$1 must be positive"
}
flag() {
  case "$2" in 0|1) :;; *) fail "$1 must be 0 or 1";; esac
}
absolute() {
  case "$2" in
    /*) [ "$2" != / ] || fail "$1 must not be /";;
    *) fail "$1 must be an absolute path";;
  esac
}
safe_path() {
  case "$2/" in
    */../*|*/./*|*//*)
      fail "$1 must not contain dot or empty path components"
      ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --dry-run) DRY_RUN=1;;
    --print-env) PRINT_ENV=1;;
    --allow-non-pi) ALLOW_NON_PI=1;;
    --full) FULL=1;;
    --bootstrap) BOOTSTRAP=1;;
    --verify) VERIFY=1;;
    --no-verify) VERIFY=0;;
    --verify-only) VERIFY_ONLY=1;;
    --offline) OFFLINE=1;;
    --benchmark) BENCHMARK=1;;
    --prefix|--destdir|--jobs|--backup-dir|--log-file|--min-memory-kib)
      option=$1
      shift
      [ "$#" -gt 0 ] || fail "missing value for $option"
      case "$option" in
        --prefix) PREFIX=$1;;
        --destdir) DESTDIR=$1;;
        --jobs) PI1_JOBS=$1;;
        --backup-dir) BACKUP_DIR=$1;;
        --log-file) LOG_FILE=$1;;
        --min-memory-kib) MIN_AVAILABLE_KIB=$1;;
      esac
      ;;
    --) shift; [ "$#" -eq 0 ] || fail 'unexpected trailing arguments'; break;;
    *) fail "unsupported option: $1";;
  esac
  shift
done

absolute PREFIX "$PREFIX"
[ -z "$DESTDIR" ] || absolute DESTDIR "$DESTDIR"
[ -z "$BACKUP_DIR" ] || absolute BACKUP_DIR "$BACKUP_DIR"
[ -z "$LOG_FILE" ] || absolute LOG_FILE "$LOG_FILE"
case "$PREFIX" in */) PREFIX=${PREFIX%/};; esac
case "$DESTDIR" in */) DESTDIR=${DESTDIR%/};; esac
case "$BACKUP_DIR" in */) BACKUP_DIR=${BACKUP_DIR%/};; esac
safe_path PREFIX "$PREFIX"
[ -z "$DESTDIR" ] || safe_path DESTDIR "$DESTDIR"
[ -z "$BACKUP_DIR" ] || safe_path BACKUP_DIR "$BACKUP_DIR"
[ -z "$LOG_FILE" ] || safe_path LOG_FILE "$LOG_FILE"
positive PI1_JOBS "$PI1_JOBS"
positive MIN_AVAILABLE_KIB "$MIN_AVAILABLE_KIB"
flag DRY_RUN "$DRY_RUN"
flag ALLOW_NON_PI "$ALLOW_NON_PI"
flag FULL "$FULL"
flag BOOTSTRAP "$BOOTSTRAP"
flag VERIFY "$VERIFY"
flag VERIFY_ONLY "$VERIFY_ONLY"
flag OFFLINE "$OFFLINE"
flag BENCHMARK "$BENCHMARK"
flag PRINT_ENV "$PRINT_ENV"
[ "$ALLOW_NON_PI" -eq 0 ] || [ -n "$DESTDIR" ] ||
  fail '--allow-non-pi requires --destdir'
[ "$VERIFY_ONLY" -eq 0 ] || [ -z "$DESTDIR" ] ||
  fail '--verify-only checks a live prefix; omit --destdir'
[ "$BOOTSTRAP" -eq 0 ] || [ -n "${BOOTSTRAP_CMD:-}" ] ||
  fail '--bootstrap requires an explicit BOOTSTRAP_CMD'

host_os=$(uname -s 2>/dev/null || printf '%s\n' unknown)
host_arch=$(uname -m 2>/dev/null || printf '%s\n' unknown)
is_pi1=0
case "$host_os:$host_arch" in
  Linux:armv6l|Linux:armv6|Linux:armv6zk) is_pi1=1;;
esac

case "$host_arch" in
  x86_64|amd64) STAGE_ARCH=amd64;;
  i386|i486|i586|i686) STAGE_ARCH=i386;;
  aarch64|arm64) STAGE_ARCH=arm64;;
  armv7l|armv7) STAGE_ARCH=armv7;;
  armv6l|armv6|armv6zk) STAGE_ARCH=armv6;;
  riscv64) STAGE_ARCH=riscv64;;
  ppc64le) STAGE_ARCH=ppc64el;;
  *) STAGE_ARCH=$host_arch;;
esac

if [ "$PRINT_ENV" -eq 1 ]; then
  printf 'PREFIX=%s\n' "$PREFIX"
  printf 'DESTDIR=%s\n' "$DESTDIR"
  printf 'PI1_JOBS=%s\n' "$PI1_JOBS"
  printf 'FULL=%s\n' "$FULL"
  printf 'VERIFY=%s\n' "$VERIFY"
  printf 'VERIFY_ONLY=%s\n' "$VERIFY_ONLY"
  printf 'OFFLINE=%s\n' "$OFFLINE"
  printf 'HOST=%s/%s\n' "$host_os" "$host_arch"
  printf 'STAGE_ARCH=%s\n' "$STAGE_ARCH"
  printf 'BACKUP_DIR=%s\n' "$BACKUP_DIR"
  exit 0
fi

if [ "$VERIFY_ONLY" -eq 1 ]; then
  installed_bin=$PREFIX/bin/vitte
  [ -x "$installed_bin" ] || fail "installed compiler not found: $installed_bin"
  "$installed_bin" --version || fail 'installed compiler version check failed'
  if [ "$FULL" -eq 1 ]; then
    [ -s "$PREFIX/share/vitte/runtime/runtime.vit" ] ||
      fail "runtime source missing below $PREFIX/share/vitte"
    [ -s "$PREFIX/share/vitte/modules/core/package.toml" ] ||
      fail "core module missing below $PREFIX/share/vitte"
  fi
  if [ "$VERIFY" -eq 1 ]; then
    smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/vitte-pi1-verify.XXXXXX") ||
      fail 'could not create verification directory'
    trap 'rm -rf -- "$smoke_dir"' EXIT HUP INT TERM
    cat > "$smoke_dir/smoke.vit" <<'VIT'
proc main() -> int {
  return 0;
}
VIT
    (
      cd "$smoke_dir"
      "$installed_bin" check smoke.vit
      "$installed_bin" compile smoke.vit -o smoke
      ./smoke
    ) || fail 'installed compiler smoke test failed'
  fi
  log "verified existing installation at $PREFIX"
  exit 0
fi

if [ "$DRY_RUN" -eq 1 ]; then
  if [ "$is_pi1" -ne 1 ] && [ "$ALLOW_NON_PI" -ne 1 ]; then
    log "dry-run host is $host_os/$host_arch; a real install requires Linux ARMv6"
  fi
  log "would build natively with $PI1_JOBS job(s), stage below ${DESTDIR:-<live>}$PREFIX, full=$FULL, verify=$VERIFY"
  exit 0
fi

if [ "$is_pi1" -ne 1 ] && [ "$ALLOW_NON_PI" -ne 1 ]; then
  fail "native Linux ARMv6 required; detected $host_os/$host_arch (use --allow-non-pi only with --destdir for host staging)"
fi

if [ -n "$LOG_FILE" ]; then
  mkdir -p "$(dirname "$LOG_FILE")" || fail "cannot create log directory for $LOG_FILE"
  : >> "$LOG_FILE" || fail "cannot write log file: $LOG_FILE"
  printf '[%s] logging to %s\n' "$SCRIPT_NAME" "$LOG_FILE"
  exec >> "$LOG_FILE" 2>&1
fi

require_tool make
require_tool mktemp
require_tool find
require_tool cp
require_tool install
require_tool date

if [ "$is_pi1" -ne 1 ]; then
  log "host staging mode enabled for $host_os/$host_arch; no ARMv6 cross-compilation is performed"
fi
if [ -r /proc/meminfo ]; then
  available_kib=$(awk '
    /^MemAvailable:/ { available = $2 }
    /^MemFree:/ { free = $2 }
    END { if (available != "") print available; else if (free != "") print free }
  ' /proc/meminfo)
  case "$available_kib" in
    ''|*[!0-9]*) :;;
    *)
      if [ "$available_kib" -lt "$MIN_AVAILABLE_KIB" ]; then
        log "warning: only ${available_kib} KiB memory available; native builds may need swap"
      fi
      ;;
  esac
fi

start_time=$(date +%s)
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/vitte-pi1-install.XXXXXX") ||
  fail 'could not create temporary staging directory'
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  rm -rf -- "$temporary_root"
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cat > "$temporary_root/commit-staged.sh" <<'COMMIT'
#!/bin/sh
set -eu

safe_path() {
  current=$1
  while :; do
    [ ! -L "$current" ] || {
      printf '[vitte-installer][error] refusing to follow symlink: %s\n' "$current" >&2
      return 1
    }
    [ "$current" != / ] || break
    parent=$(dirname "$current")
    [ "$parent" != "$current" ] || break
    current=$parent
  done
}

restore_changes() {
  [ -f "$backup_root/.changed" ] && [ -f "$backup_root/.present" ] || {
    printf '[vitte-installer][error] rollback metadata missing in %s\n' "$backup_root" >&2
    return 1
  }
  result=0
  while IFS= read -r relative; do
    [ -n "$relative" ] || continue
    destination=$destination_root/$relative
    if ! safe_path "$destination"; then
      result=1
    elif grep -Fqx "$relative" "$backup_root/.present"; then
      if ! mkdir -p "$(dirname "$destination")" ||
         ! cp -p "$backup_root/$relative" "$destination"
      then
        printf '[vitte-installer][error] could not restore %s\n' "$destination" >&2
        result=1
      fi
    elif ! rm -f "$destination"; then
      printf '[vitte-installer][error] could not remove partial file %s\n' "$destination" >&2
      result=1
    fi
  done < "$backup_root/.changed"
  return "$result"
}

if [ "${1:-}" = --rollback ]; then
  destination_root=$2
  backup_root=$3
  restore_changes
  exit $?
fi

source_root=$1
destination_root=$2
backup_root=$3

[ -d "$source_root" ] || {
  printf '[vitte-installer][error] staged source directory missing: %s\n' "$source_root" >&2
  exit 1
}
[ ! -e "$backup_root" ] || {
  printf '[vitte-installer][error] backup directory already exists: %s\n' "$backup_root" >&2
  exit 1
}

safe_path "$destination_root"
safe_path "$backup_root"
mkdir -p "$destination_root" "$backup_root"
files=$backup_root/.files
present=$backup_root/.present
changed=$backup_root/.changed
find "$source_root" -type f -print > "$files"
: > "$present"
: > "$changed"

rollback() {
  status=$?
  trap - EXIT HUP INT TERM
  if [ "$status" -ne 0 ] && ! restore_changes; then
    printf '[vitte-installer][error] automatic rollback was incomplete; inspect %s\n' "$backup_root" >&2
  fi
  exit "$status"
}
trap rollback EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

while IFS= read -r source; do
  case "$source" in
    "$source_root"/*) relative=${source#"$source_root"/};;
    *) printf '[vitte-installer][error] invalid staged path: %s\n' "$source" >&2; exit 1;;
  esac

  destination=$destination_root/$relative
  saved=$backup_root/$relative
  safe_path "$destination"
  if [ -L "$destination" ]; then
    printf '[vitte-installer][error] refusing to replace symlink: %s\n' "$destination" >&2
    exit 1
  fi
  if [ -e "$destination" ]; then
    [ -f "$destination" ] || {
      printf '[vitte-installer][error] refusing to replace non-file: %s\n' "$destination" >&2
      exit 1
    }
    mkdir -p "$(dirname "$saved")"
    cp -p "$destination" "$saved"
    printf '%s\n' "$relative" >> "$present"
  fi

  mkdir -p "$(dirname "$destination")"
  printf '%s\n' "$relative" >> "$changed"
  cp -p "$source" "$destination"
done < "$files"

trap - EXIT HUP INT TERM
COMMIT
chmod 0755 "$temporary_root/commit-staged.sh"

if [ "$BOOTSTRAP" -eq 1 ]; then
  log 'running caller-supplied bootstrap command'
  (cd "$ROOT_DIR" && sh -c "$BOOTSTRAP_CMD") ||
    fail 'bootstrap command failed'
fi

MAKE_CMD=${MAKE:-make}
log "building Vitte natively with $PI1_JOBS job(s)"
"$MAKE_CMD" -C "$ROOT_DIR" -j "$PI1_JOBS" all

staged_root=$temporary_root/stage
mkdir -p "$staged_root"
if [ "$FULL" -eq 1 ]; then
  [ -x "$ROOT_DIR/build/bin/vitte" ] ||
    fail "native compiler missing after build: $ROOT_DIR/build/bin/vitte"
  VERSION=$(scripts_build_package_version) \
  VITTE_BIN="$ROOT_DIR/build/bin/vitte" \
    "$ROOT_DIR/scripts/stage-installer-payload.sh" \
    "$staged_root" linux "$STAGE_ARCH" unix
  source_prefix=$staged_root/usr/local
  [ -d "$source_prefix" ] || fail 'full payload did not create usr/local'
  staged_prefix=$staged_root$PREFIX
  if [ "$PREFIX" != /usr/local ]; then
    mkdir -p "$staged_prefix"
    cp -Rp "$source_prefix/." "$staged_prefix/"
  else
    staged_prefix=$source_prefix
  fi
else
  "$MAKE_CMD" -C "$ROOT_DIR" install \
    PREFIX="$PREFIX" \
    DESTDIR="$staged_root"
  staged_prefix=$staged_root$PREFIX
fi

staged_bin=$staged_prefix/bin/vitte
[ -x "$staged_bin" ] || fail "staged compiler missing: $staged_bin"
if [ "$FULL" -eq 1 ]; then
  [ -s "$staged_prefix/share/vitte/runtime/runtime.vit" ] ||
    fail 'full payload is missing the Vitte runtime'
  [ -s "$staged_prefix/share/vitte/modules/core/package.toml" ] ||
    fail 'full payload is missing the core module'
fi

if [ "$VERIFY" -eq 1 ]; then
  smoke_dir=$temporary_root/smoke
  mkdir -p "$smoke_dir"
  cat > "$smoke_dir/smoke.vit" <<'VIT'
proc main() -> int {
  return 0;
}
VIT
  (
    cd "$smoke_dir"
    "$staged_bin" --version
    "$staged_bin" check smoke.vit
    "$staged_bin" compile smoke.vit -o smoke
    ./smoke
  ) || fail 'staged compiler smoke test failed'
fi

destination_prefix=${DESTDIR}${PREFIX}
if [ -n "$DESTDIR" ]; then
  backup_root=$temporary_root/stage-backup
  log "committing staged files below $destination_prefix"
  sh "$temporary_root/commit-staged.sh" "$staged_prefix" "$destination_prefix" "$backup_root"
else
  if [ -n "$BACKUP_DIR" ]; then
    backup_root=$BACKUP_DIR/raspberry-pi1-$(date -u +%Y%m%dT%H%M%SZ)-$$
  else
    backup_root=${HOME:-/tmp}/.local/state/vitte/backups/raspberry-pi1-$(date -u +%Y%m%dT%H%M%SZ)-$$
  fi
  case "$backup_root/" in
    "$PREFIX/"*|"$ROOT_DIR/"*) fail 'backup directory must be outside the install prefix and source tree';;
  esac
  [ ! -e "$backup_root" ] || fail "backup directory already exists: $backup_root"
  mkdir -p "$(dirname "$backup_root")" ||
    fail "cannot create backup parent: $(dirname "$backup_root")"
  log "backing up replaced files to $backup_root"
  if [ "$(id -u)" -eq 0 ]; then
    sh "$temporary_root/commit-staged.sh" "$staged_prefix" "$destination_prefix" "$backup_root"
  else
    require_tool sudo
    sudo sh "$temporary_root/commit-staged.sh" "$staged_prefix" "$destination_prefix" "$backup_root"
  fi
fi

rollback_install() {
  if [ -n "$DESTDIR" ]; then
    sh "$temporary_root/commit-staged.sh" --rollback "$destination_prefix" "$backup_root"
  elif [ "$(id -u)" -eq 0 ]; then
    sh "$temporary_root/commit-staged.sh" --rollback "$destination_prefix" "$backup_root"
  else
    sudo sh "$temporary_root/commit-staged.sh" --rollback "$destination_prefix" "$backup_root"
  fi
}

if [ "$VERIFY" -eq 1 ]; then
  verify_bin=$destination_prefix/bin/vitte
  if [ ! -x "$verify_bin" ] || ! "$verify_bin" --version; then
    log "post-install verification failed; restoring files from $backup_root"
    rollback_install || fail "post-install verification and rollback both failed; inspect $backup_root"
    fail 'post-install verification failed; the previous installation was restored'
  fi
fi

log "installation completed at $destination_prefix"
if [ -z "$DESTDIR" ]; then
  log "backup retained at $backup_root"
fi
if [ "$BENCHMARK" -eq 1 ]; then
  end_time=$(date +%s)
  log "elapsed_seconds=$((end_time - start_time))"
fi
