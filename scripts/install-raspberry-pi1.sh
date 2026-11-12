#!/bin/sh
# Vitte native Raspberry Pi 1 installer. POSIX sh.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
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
SMOKE_SCRIPT=${SMOKE_SCRIPT:-$ROOT_DIR/scripts/ci/real-install-smoke.sh}

usage() {
  cat <<'HELP'
usage: install-raspberry-pi1.sh [options]
  --prefix DIR          install prefix (default /usr/local)
  --destdir DIR         stage only, never change live prefix
  --jobs N              native build jobs (default 1)
  --full                require runtime, stdlib and modules in staged payload
  --bootstrap           run repository bootstrap hook (see README)
  --verify              run staged compiler smoke test (default)
  --no-verify           skip smoke test (not recommended)
  --verify-only         check existing installation without building
  --offline             prohibit installer network operations (default)
  --benchmark           report elapsed build time
  --backup-dir DIR      save pre-install overwritten files here
  --log-file FILE       append installer output to log (via tee if available)
  --allow-non-pi        only with --destdir; never run staged binaries
  --min-memory-kib N    low-memory warning threshold (default 65536)
  --dry-run             show intended actions without building
  --print-env           show effective configuration
  --help

Safety: live installs require a backup and use a file-level rollback on failure.
Rollback covers files replaced by the staged install, not arbitrary maintainer
scripts or unrelated system changes. No automatic swap configuration.
HELP
}
fail() { printf '[%s][error] %s\n' "$SCRIPT_NAME" "$*" >&2; exit 1; }
log() { printf '[%s] %s\n' "$SCRIPT_NAME" "$*"; }
require_tool() { command -v "$1" >/dev/null 2>&1 || fail "missing tool: $1"; }
positive() { case "$2" in ''|*[!0-9]*) fail "$1 must be a positive integer";; esac; [ "$2" -gt 0 ] || fail "$1 must be positive"; }
flag() { case "$2" in 0|1) :;; *) fail "$1 must be 0 or 1";; esac; }
absolute() { case "$2" in /*) [ "$2" != / ] || fail "$1 must not be /";; *) fail "$1 must be an absolute path";; esac; }

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
      opt=$1; shift; [ "$#" -gt 0 ] || fail "missing value for $opt"
      case "$opt" in
        --prefix) PREFIX=$1;; --destdir) DESTDIR=$1;; --jobs) PI1_JOBS=$1;;
        --backup-dir) BACKUP_DIR=$1;; --log-file) LOG_FILE=$1;;
        --min-memory-kib) MIN_AVAILABLE_KIB=$1;;
      esac;;
    --) shift; [ "$#" -eq 0 ] || fail 'unexpected trailing arguments'; break;;
    *) fail "unsupported option: $1";;
  esac
  shift
done
absolute PREFIX "$PREFIX"
[ -z "$DESTDIR" ] || absolute DESTDIR "$DESTDIR"
[ -z "$BACKUP_DIR" ] || absolute BACKUP_DIR "$BACKUP_DIR"
[ -z "$LOG_FILE" ] || absolute LOG_FILE "$LOG_FILE"
positive PI1_JOBS "$PI1_JOBS"
positive MIN_AVAILABLE_KIB "$MIN_AVAILABLE_KIB"
for name in DRY_RUN ALLOW_NON_PI FULL BOOTSTRAP VERIFY VERIFY_ONLY OFFLINE BENCHMARK PRINT_ENV; do
  eval "value=\${$name}"
  flag "$name" "$value"
done
[ "$ALLOW_NON_PI" -eq 0 ] || [ -n "$DESTDIR" ] || fail '--allow-non-pi requires --destdir'
[ "$VERIFY_ONLY" -eq 0 ] || [ -z "$DESTDIR" ] || fail '--verify-only checks a live prefix; omit --destdir'

if [ "$PRINT_ENV" -eq 1 ]; then
