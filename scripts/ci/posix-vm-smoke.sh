#!/bin/sh
set -eu

GMAKE=${GMAKE:-}
CC=${CC:-cc}

if [ -z "$GMAKE" ]; then
  if command -v gmake >/dev/null 2>&1; then
    GMAKE=gmake
  elif make --version 2>/dev/null | grep -q 'GNU Make'; then
    GMAKE=make
  else
    printf '[POSIX-VM][error] GNU make is required (set GMAKE to its path)\n' >&2
    exit 1
  fi
fi

printf '[POSIX-VM] host: '
uname -a
printf '[POSIX-VM] make: '
command -v "$GMAKE"
printf '[POSIX-VM] C compiler: '
command -v "$CC"

"$GMAKE" MODE=release CC="$CC" SHELL=/bin/sh -j2
build/bin/vitte --version
build/bin/vitte help >/dev/null
build/bin/vitte check examples/hello.vit --quiet
"$GMAKE" MODE=release CC="$CC" SHELL=/bin/sh test-runtime
VITTE_BIN="$PWD/build/bin/vitte" \
  VITTE_BOOTSTRAP_BIN="$PWD/build/bin/vitte-bootstrap" \
  sh scripts/ci/package-install-smoke.sh
