#!/bin/sh
set -u

stage='unknown'
step='unknown step'
reproduce='make bootstrap-selfhost'
compilers=''
generated=''
artifacts=''
evidence_dir=''
build_options='not supplied'
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

while [ "$#" -gt 0 ]; do
  case "$1" in
    --stage)
      [ "$#" -ge 2 ] || exit 2
      stage=$2
      shift 2
      ;;
    --step)
      [ "$#" -ge 2 ] || exit 2
      step=$2
      shift 2
      ;;
    --compiler)
      [ "$#" -ge 2 ] || exit 2
      compilers="${compilers}${compilers:+
}  $2"
      shift 2
      ;;
    --generated)
      [ "$#" -ge 2 ] || exit 2
      generated="${generated}${generated:+
}  $2"
      shift 2
      ;;
    --artifact)
      [ "$#" -ge 2 ] || exit 2
      artifacts="${artifacts}${artifacts:+
}  $2"
      shift 2
      ;;
    --evidence-dir)
      [ "$#" -ge 2 ] || exit 2
      evidence_dir=$2
      shift 2
      ;;
    --options)
      [ "$#" -ge 2 ] || exit 2
      build_options=$2
      shift 2
      ;;
    --reproduce)
      [ "$#" -ge 2 ] || exit 2
      reproduce=$2
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *)
      printf 'run-selfhost-step: unexpected option: %s\n' "$1" >&2
      exit 2
      ;;
  esac
done

if [ "$#" -eq 0 ]; then
  printf '%s\n' 'run-selfhost-step: missing command after --' >&2
  exit 2
fi

if [ -z "$evidence_dir" ]; then
  printf '%s\n' 'run-selfhost-step: missing --evidence-dir' >&2
  exit 2
fi

make_dry_run=no
for make_flag in ${MAKEFLAGS:-}; do
  case "$make_flag" in
    --just-print|--dry-run|-n)
      make_dry_run=yes
      ;;
    --*)
      ;;
    *=*)
      ;;
    -*)
      case "${make_flag#-}" in *n*) make_dry_run=yes ;; esac
      ;;
    *)
      case "$make_flag" in *[!A-Za-z]*) ;; *n*) make_dry_run=yes ;; esac
      ;;
  esac
done
if [ "$make_dry_run" = yes ]; then
  printf '[SELFHOST][DRY-RUN] Stage %s would run %s\n' "$stage" "$step"
  printf '  Reproduce with: %s\n  Command arguments:\n' "$reproduce"
  for argument do
    printf '    %s\n' "$argument"
  done
  exit 0
fi

slug=$(printf '%s' "$step" | LC_ALL=C tr '[:upper:] ' '[:lower:]-' | tr -cd 'a-z0-9_-')
mkdir -p "$evidence_dir" || {
  printf '[SELFHOST][ERROR] Stage %s cannot create evidence directory: %s\n' \
    "$stage" "$evidence_dir" >&2
  exit 125
}

log="$evidence_dir/$slug.log"
manifest="$evidence_dir/$slug.txt"
summary="$evidence_dir/stage-$stage-summary.txt"
status_file="$evidence_dir/.$slug.$$.status"
(
  env "$@"
  command_status=$?
  printf '%s\n' "$command_status" > "$status_file"
  exit "$command_status"
) 2>&1 | tee "$log"
tee_status=$?
if [ -f "$status_file" ]; then
  status=$(cat "$status_file")
else
  status=125
fi
unlink "$status_file" 2>/dev/null || :
if [ "$tee_status" -ne 0 ] && [ "$status" -eq 0 ]; then
  status=125
fi

version=$(cat "$root_dir/VERSION" 2>/dev/null || printf unknown)
commit=$(git -C "$root_dir" rev-parse HEAD 2>/dev/null || printf unknown)
recorded_at=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || printf unknown)
if [ -n "$(git -C "$root_dir" status --porcelain 2>/dev/null)" ]; then
  working_tree=dirty
else
  working_tree=clean
fi
if ! {
  printf 'Stage: %s\nStep: %s\nResult: %s (exit %s)\n' \
    "$stage" "$step" "$([ "$status" -eq 0 ] && printf PASSED || printf FAILED)" "$status"
  printf 'Recorded at UTC: %s\nVitte version: %s\nCommit: %s\nWorking tree: %s\n' \
    "$recorded_at" "$version" "$commit" "$working_tree"
  printf 'Build options: %s\n' "$build_options"
  printf 'Compiler(s):\n%s\n' "${compilers:-  not applicable}"
  printf 'Generated source path(s):\n%s\n' "${generated:-  none}"
  printf 'Stage artifact path(s):\n%s\n' "${artifacts:-  none}"
  printf 'Reproduce: %s\n' "$reproduce"
  printf 'Command arguments:\n'
  for argument do
    printf '  %s\n' "$argument"
  done
  printf 'Output log: %s\n' "$log"
} > "$manifest"; then
  printf '[SELFHOST][ERROR] Could not write evidence manifest: %s\n' "$manifest" >&2
  [ "$status" -ne 0 ] || status=125
fi

if [ ! -f "$summary" ]; then
  {
    printf 'Recorded at UTC: %s\nVitte version: %s\nCommit: %s\nWorking tree: %s\n' \
      "$recorded_at" "$version" "$commit" "$working_tree"
    printf 'Build options: %s\n' "$build_options"
    printf 'Stage %s evidence files\n' "$stage"
  } > "$summary" || {
    printf '[SELFHOST][ERROR] Could not write stage evidence index: %s\n' "$summary" >&2
    [ "$status" -ne 0 ] || status=125
  }
fi
printf 'Step: %s | %s (exit %s)\n  manifest: %s\n  log: %s\n' \
  "$step" "$([ "$status" -eq 0 ] && printf PASSED || printf FAILED)" \
  "$status" "$manifest" "$log" >> "$summary" || {
  printf '[SELFHOST][ERROR] Could not update stage evidence index: %s\n' "$summary" >&2
  [ "$status" -ne 0 ] || status=125
}

if [ "$status" -ne 0 ]; then
  printf '\n[SELFHOST][ERROR] Stage %s failed during %s (exit %s).\n' \
    "$stage" "$step" "$status" >&2
  if [ -n "$compilers" ]; then
    printf '  Compiler(s) used:\n%s\n' "$compilers" >&2
  else
    printf '  Compiler used: not applicable to this step\n' >&2
  fi
  if [ -n "$generated" ]; then
    printf '  Generated source path(s):\n%s\n' "$generated" >&2
  else
    printf '  Generated source path(s): none for this step\n' >&2
  fi
  if [ -n "$artifacts" ]; then
    printf '  Stage artifact path(s):\n%s\n' "$artifacts" >&2
  fi
  printf '  Reproduce the stage with: %s\n' "$reproduce" >&2
  printf '  Evidence manifest: %s\n' "$manifest" >&2
  printf '  Output log: %s\n' "$log" >&2
else
  printf '[SELFHOST] evidence saved: %s\n' "$manifest"
fi

exit "$status"
