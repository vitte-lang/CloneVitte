#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
MODE=${MODE:-full}
[ "$MODE" = full ] || { echo 'Only MODE=full supported' >&2; exit 2; }
DEB=${DEB:-}
BOOTSTRAP_CMD=${BOOTSTRAP_CMD:-}
case ${1:-} in
  --help|-h)
    echo 'Usage: DEB=/path/to/vitte.deb [BOOTSTRAP_CMD="..."] scripts/install-vitte-full.sh [--dry-run]'
    echo 'Bootstrap is optional but requires an explicit validated command.'
    exit 0 ;;
  --dry-run) DRY=1 ;;
  '') DRY=0 ;;
  *) echo 'Unknown argument' >&2; exit 2 ;;
esac
[ -n "$DEB" ] && [ -f "$DEB" ] || { echo 'Set DEB to an existing .deb package' >&2; exit 2; }
command -v dpkg-deb >/dev/null || { echo 'dpkg-deb required' >&2; exit 2; }
dpkg-deb --info "$DEB" >/dev/null
command -v python3 >/dev/null || { echo 'python3 required' >&2; exit 2; }
[ -f "$ROOT/scripts/vitte-editor-setup.py" ] || { echo 'Missing editor setup script' >&2; exit 2; }
python3 "$ROOT/scripts/vitte-editor-setup.py" --source "$ROOT" --dry-run
printf 'Package: %s\n' "$DEB"
if [ "$DRY" -eq 1 ]; then
  echo 'Dry-run: no package or configuration changes made.'
  exit 0
fi
# Stage 0 compiler must remain intact until all checks pass.
if [ -n "$BOOTSTRAP_CMD" ]; then
  echo 'Running explicitly supplied bootstrap before package installation.'
  (cd "$ROOT" && sh -c "$BOOTSTRAP_CMD") || { echo 'Bootstrap failed; package not installed' >&2; exit 1; }
else
  echo 'Bootstrap requested but no BOOTSTRAP_CMD provided: SKIPPED (no safe command known).'
fi
# APT/dpkg handles package-level upgrade and rollback separately from user config.
echo 'Installing package with APT (dependencies resolved)...'
sudo apt install "$DEB"
# Verify actual selected binary, not only the package payload.
command -v vitte >/dev/null || { echo 'Vitte binary missing from PATH' >&2; exit 1; }
command -v vittec >/dev/null || { echo 'Vitte compiler binary missing from PATH' >&2; exit 1; }
vitte --version
printf 'Selected executable: %s\n' "$(command -v vitte)"
python3 "$ROOT/scripts/vitte-editor-setup.py" --source "$ROOT"
echo 'Vitte full installation finished. Editor transaction stored under ~/.local/state/vitte/editor-setup.'
