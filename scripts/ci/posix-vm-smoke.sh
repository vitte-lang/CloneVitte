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

# The bootstrap driver launches `cc` directly; make and the native driver use
# CC. Put the selected compiler behind that portable command name as well.
if [ "$CC" != "cc" ]; then
  cc_path=$(command -v "$CC")
  cc_shim_dir="${TMPDIR:-/tmp}/vitte-posix-cc-$$"
  mkdir "$cc_shim_dir"
  ln -s "$cc_path" "$cc_shim_dir/cc"
  PATH="$cc_shim_dir:$PATH"
  export PATH
  trap 'rm -f "$cc_shim_dir/cc"; rmdir "$cc_shim_dir" 2>/dev/null || true' 0
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
