#!/bin/sh
set -eu

[ "$#" -ge 1 ] || {
  printf '[bsd-installer][error] missing BSD family\n' >&2
  exit 2
}
FAMILY=$1
DEFAULT_RELEASE=all
if [ "$#" -ge 2 ]; then
  DEFAULT_RELEASE=$2
  shift 2
else
  shift
fi
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$SCRIPT_DIR

while [ "$ROOT_DIR" != / ]; do
  if [ -f "$ROOT_DIR/scripts/build-bsd-installers.sh" ] &&
     [ -f "$ROOT_DIR/scripts/build-freebsd-packages.sh" ]
  then
    break
  fi
  ROOT_DIR=$(dirname "$ROOT_DIR")
done

[ -f "$ROOT_DIR/scripts/build-bsd-installers.sh" ] || {
  printf '[bsd-installer][error] Vitte repository root not found from %s\n' \
    "$SCRIPT_DIR" >&2
  exit 1
}

if [ "$FAMILY" = auto ]; then
  FAMILY=${BSD_FAMILY:-}
  if [ -z "$FAMILY" ]; then
    case "$(uname -s 2>/dev/null || printf '%s\n' unknown)" in
      FreeBSD) FAMILY=freebsd;;
      DragonFly) FAMILY=dragonfly;;
      OpenBSD) FAMILY=openbsd;;
      NetBSD) FAMILY=netbsd;;
      MidnightBSD) FAMILY=midnightbsd;;
      GhostBSD) FAMILY=ghostbsd;;
      NomadBSD) FAMILY=nomadbsd;;
      helloSystem) FAMILY=hellosystem;;
      *) printf '[bsd-installer][error] set BSD_FAMILY on this host\n' >&2; exit 1;;
    esac
  fi
fi

ARCH=${ARCH:-$(uname -m 2>/dev/null || printf '%s\n' amd64)}
export ARCH

case "$FAMILY" in
  all) DEFAULT_RELEASE=${BSD_RELEASE:-all};;
  freebsd)
    exec sh "$ROOT_DIR/scripts/build-freebsd-packages.sh" "$@"
    ;;
  dragonfly) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-6.4}};;
  openbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-7.8}};;
  netbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-10}};;
  midnightbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-3.0}};;
  ghostbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-24}};;
  hardenedbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-14}};;
  nomadbsd) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-141}};;
  hellosystem) DEFAULT_RELEASE=${BSD_RELEASE:-${DEFAULT_RELEASE:-0.8}};;
  *) printf '[bsd-installer][error] unsupported BSD family: %s\n' "$FAMILY" >&2; exit 1;;
esac

BSD_FAMILY=$FAMILY BSD_RELEASE=$DEFAULT_RELEASE FAMILY=bsd \
  exec sh "$ROOT_DIR/scripts/build-bsd-installers.sh" "$@"
