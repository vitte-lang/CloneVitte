#!/bin/sh
set -eu
set -f

mode=${1:?usage: architecture-smoke.sh run|build|objects|rv32-object ARCHITECTURE}
architecture=${2:?missing architecture name}
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root_dir"

GMAKE=${GMAKE:-make}
ARCH_CC=${ARCH_CC:-${CC:-cc}}
ARCH_CFLAGS=${ARCH_CFLAGS:-}
ARCH_RUNNER=${ARCH_RUNNER:-}
ARCH_SYSROOT=${ARCH_SYSROOT:-}
ARCH_EXE_SUFFIX=${ARCH_EXE_SUFFIX:-}
if [ "${ARCH_LDLIBS+x}" != x ]; then
  case $(uname -s) in
    MINGW* | MSYS* | CYGWIN*) ARCH_LDLIBS= ;;
    *) ARCH_LDLIBS=-lm ;;
  esac
fi
jobs=${ARCH_JOBS:-2}

case $(uname -s) in
  MINGW* | MSYS* | CYGWIN*) host_exe_suffix=.exe ;;
  *) host_exe_suffix= ;;
esac

host_vitte="$root_dir/build/bin/vitte$host_exe_suffix"
frontend_c="$root_dir/build/native-frontend/api-implementation.c"
build_dir="$root_dir/build/architecture/$architecture"
target_vitte="$build_dir/bin/vitte$ARCH_EXE_SUFFIX"

printf '[ARCH] host: '
uname -a
printf '[ARCH] target: %s (%s)\n' "$architecture" "$mode"

# Build once for the host so its Vitte seed emits the frontend C used by the
# target build. The target compiler is never executed during cross compilation.
"$GMAKE" MODE=release -j"$jobs" all
test -x "$host_vitte"
test -s "$frontend_c"

target_cc="$ARCH_CC"
if [ -n "$ARCH_CFLAGS" ]; then
  target_cc="$target_cc $ARCH_CFLAGS"
fi
target_cflags="-std=c17 -O2 -DNDEBUG"
if [ -n "$ARCH_CFLAGS" ]; then
  target_cflags="$target_cflags $ARCH_CFLAGS"
fi

case "$mode" in
  run | build)
    "$GMAKE" MODE=release BUILD="$build_dir" CC="$target_cc" \
      CFLAGS="$target_cflags" \
      NATIVE_FRONTEND_COMPILER="$host_vitte" \
      NATIVE_FRONTEND_C="$frontend_c" \
      -j"$jobs" all
    test -s "$target_vitte"
    if [ -n "${ARCH_EXPECT_FILE:-}" ]; then
      file "$target_vitte" | tee "$build_dir/architecture.txt"
      grep -F "$ARCH_EXPECT_FILE" "$build_dir/architecture.txt" >/dev/null
    fi
    ;;
  objects)
    "$GMAKE" MODE=release BUILD="$build_dir" CC="$target_cc" \
      CFLAGS="$target_cflags" \
      NATIVE_FRONTEND_COMPILER="$host_vitte" \
      NATIVE_FRONTEND_C="$frontend_c" \
      -j"$jobs" bootstrap-archive \
      "$build_dir/obj/main.o" \
      "$build_dir/native-frontend/api-implementation.o"
    if [ -n "${ARCH_EXPECT_FILE:-}" ]; then
      file "$build_dir/obj/main.o" | tee "$build_dir/architecture.txt"
      grep -F "$ARCH_EXPECT_FILE" "$build_dir/architecture.txt" >/dev/null
    fi
    ;;
  rv32-object)
    mkdir -p "$build_dir"
    generated_c="$build_dir/native-selfhost-smoke.c"
    "$host_vitte" compile src/tests/native_selfhost_smoke.vit \
      --emit-c --quiet --no-color -o "$generated_c"
    set -- ${ARCH_CFLAGS:-}
    "$ARCH_CC" "$@" -ffreestanding -fno-builtin -c "$generated_c" \
      -o "$build_dir/native-selfhost-smoke.o"
    file "$build_dir/native-selfhost-smoke.o" | tee "$build_dir/architecture.txt"
    grep -E 'ELF 32-bit.*RISC-V' "$build_dir/architecture.txt" >/dev/null
    ;;
  *)
    printf '[ARCH][error] unknown test mode: %s\n' "$mode" >&2
    exit 2
    ;;
esac

if [ "$mode" = build ] || [ "$mode" = objects ] || [ "$mode" = rv32-object ]; then
  exit 0
fi

run_target() {
  if [ -n "$ARCH_RUNNER" ]; then
    if [ -n "$ARCH_SYSROOT" ]; then
      "$ARCH_RUNNER" -L "$ARCH_SYSROOT" "$@"
    else
      "$ARCH_RUNNER" "$@"
    fi
  else
    "$@"
  fi
}

run_target "$target_vitte" --version
run_target "$target_vitte" help >/dev/null
run_target "$target_vitte" check examples/hello.vit --quiet

generated_c="$build_dir/native-selfhost-smoke.c"
generated_exe="$build_dir/native-selfhost-smoke$ARCH_EXE_SUFFIX"
run_target "$target_vitte" compile src/tests/native_selfhost_smoke.vit \
  --emit-c --quiet --no-color -o "$generated_c"
set -- ${ARCH_CFLAGS:-}
"$ARCH_CC" "$@" "$generated_c" ${ARCH_LDLIBS:-} -o "$generated_exe"
run_target "$generated_exe"

printf '[ARCH] compiler, CLI and generated C smoke passed for %s\n' "$architecture"
