#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=verify-installers

. "$ROOT_DIR/scripts/common.sh"

VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}

case "$OUT_DIR" in
  /*) ;;
  *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;;
esac

STRICT_NATIVE=${STRICT_NATIVE:-0}
FREEBSD_MAJOR=${FREEBSD_MAJOR:-14}
VERIFY_METADATA=${VERIFY_METADATA:-0}
RELEASE_INSTALLER_GATE=${RELEASE_INSTALLER_GATE:-0}

die() {
  scripts_build_die "$*"
}

require_file() {
  required_path=$1

  [ -f "$required_path" ] ||
    die "missing artifact: $required_path"

  [ -s "$required_path" ] ||
    die "empty artifact: $required_path"
}

verify_sum() {
  checksum_target=$1
  checksum_file=$checksum_target.sha256

  require_file "$checksum_target"
  require_file "$checksum_file"

  scripts_build_sha256_check \
    "$checksum_target" \
    "$checksum_file"
}

verify_optional_sum() {
  file=$1

  [ ! -e "$file" ] ||
    verify_sum "$file"
}

verify_manifest_if_present() {
  artifact_path=$1
  manifest_path=$artifact_path.MANIFEST.json

  [ ! -e "$manifest_path" ] || {
    require_file "$manifest_path"

    python3 - "$artifact_path" "$manifest_path" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

artifact = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])

value = json.loads(
    manifest_path.read_text(encoding="utf-8")
)

required = (
    "schema",
    "name",
    "version",
    "os",
    "arch",
    "abi",
    "libc",
    "minimum_version",
    "sha256",
    "installed_commands",
    "contents",
)

for key in required:
    if key not in value:
        raise SystemExit(
            f"{manifest_path.name}: missing {key}"
        )

if value["name"] != artifact.name:
    raise SystemExit(
        f"{manifest_path.name}: artifact name mismatch"
    )

actual = hashlib.sha256(
    artifact.read_bytes()
).hexdigest()

if value["sha256"] != actual:
    raise SystemExit(
        f"{manifest_path.name}: sha256 mismatch"
    )

if "vitte" not in value["installed_commands"]:
    raise SystemExit(
        f"{manifest_path.name}: vitte command missing"
    )
PY
  }
}

verify_relocatable_wrapper() {
  label=$1
  command=$2
  wrapper_text=$3

  [ -n "$wrapper_text" ] ||
    die "$label wrapper is empty for $command"

  printf '%s\n' "$wrapper_text" |
    grep -F 'VITTE_ROOT' >/dev/null ||
    die "$label wrapper does not configure VITTE_ROOT for $command"

  printf '%s\n' "$wrapper_text" |
    grep -F 'libexec/vitte' >/dev/null ||
    die "$label wrapper does not reference libexec/vitte for $command"

  printf '%s\n' "$wrapper_text" |
    grep -F "$command" >/dev/null ||
    die "$label wrapper does not reference $command"
}

verify_deb() {
  arch=$1
  file=$OUT_DIR/vitte_${VERSION}_${arch}.deb

  [ -e "$file" ] || return 0

  printf '[verify-installers] Debian %s\n' "$arch"

  verify_sum "$file"
  verify_manifest_if_present "$file"

  tmp=$(mktemp -d)

  cleanup_deb() {
    rm -rf "$tmp"
  }

  trap cleanup_deb EXIT HUP INT TERM

  (
    cd "$tmp"
    ar -x "$file"
  )

  for member in \
    debian-binary \
    control.tar.gz \
    data.tar.gz
  do
    require_file "$tmp/$member"
  done

  control_listing=$tmp/control.list
  data_listing=$tmp/data.list

  bsdtar -tf "$tmp/control.tar.gz" \
    > "$control_listing"

  bsdtar -tf "$tmp/data.tar.gz" \
    > "$data_listing"

  for member in \
    control \
    md5sums \
    postinst \
    prerm \
    postrm
  do
    grep -Eq "^(\./)?${member}$" \
      "$control_listing" ||
      die "Debian package missing $member: $file"
  done

  for command in \
    vitte \
    vittec
  do
    grep -Eq "^(\./)?usr/local/bin/${command}$" \
      "$data_listing" ||
      die "Debian package missing $command: $file"

    wrapper_text=$(
      bsdtar -xOf \
        "$tmp/data.tar.gz" \
        "./usr/local/bin/$command" \
        2>/dev/null ||
        true
    )

    verify_relocatable_wrapper \
      Debian \
      "$command" \
      "$wrapper_text"
  done

  grep -Eq '^(\./)?usr/local/bin/vitte-installer-doctor$' \
    "$data_listing" ||
    die "Debian package missing installer doctor: $file"

  for required in \
    usr/local/share/vitte/editors \
    usr/local/share/vitte/completions \
    usr/local/share/vitte/assets/logo.png
  do
    grep -Eq "^(\./)?${required}(/|$)" \
      "$data_listing" ||
      die "Debian package missing $required: $file"
  done

  cleanup_deb
  trap - EXIT HUP INT TERM
}

verify_freebsd() {
  arch=$1
  abi_arch=$2

  file=$OUT_DIR/vitte-${VERSION}-freebsd-${arch}.pkg

  [ -e "$file" ] || return 0

  printf '[verify-installers] FreeBSD %s\n' "$arch"

  verify_sum "$file"
  verify_manifest_if_present "$file"

  listing=$(mktemp)

  cleanup_freebsd() {
    rm -f "$listing"
  }

  trap cleanup_freebsd EXIT HUP INT TERM

  bsdtar -tf "$file" > "$listing"

  for required in \
    +MANIFEST \
    +COMPACT_MANIFEST \
    +POST_INSTALL \
    +PRE_DEINSTALL \
    +POST_DEINSTALL
  do
    grep -Fx "$required" "$listing" >/dev/null ||
      die "FreeBSD package missing $required: $file"
  done

  bsdtar -xOf "$file" +COMPACT_MANIFEST |
    python3 -c '
import json
import sys

value = json.load(sys.stdin)
expected = sys.argv[1]

if value.get("abi") != expected:
    raise SystemExit(
        f"expected ABI {expected!r}, got {value.get('abi')!r}"
    )
' "FreeBSD:$FREEBSD_MAJOR:$abi_arch" ||
    die "wrong FreeBSD ABI: $file"

  cleanup_freebsd
  trap - EXIT HUP INT TERM
}

verify_bsd_kits() {
  for bsd_kit in \
    "$OUT_DIR"/vitte-"$VERSION"-*-*-*-installer.tar.xz
  do
    [ -e "$bsd_kit" ] || continue

    printf '[verify-installers] BSD %s\n' \
      "$(basename "$bsd_kit")"

    verify_sum "$bsd_kit"
    verify_manifest_if_present "$bsd_kit"

    listing=$(mktemp)

    scripts_build_tar_list_xz "$bsd_kit" \
      > "$listing"

    for required in \
      root/usr/local/share/vitte/assets/logo.png \
      root/usr/local/share/vitte/tools/install-unix-integrations.sh \
      root/usr/local/share/vitte/completions/bash/vitte \
      root/usr/local/share/vitte/completions/fish/vitte.fish \
      root/usr/local/share/vitte/completions/zsh/_vitte \
      uninstall.sh \
      install.sh \
      INSTALL.txt
    do
      grep -Fx "$required" "$listing" >/dev/null ||
        die "BSD installer missing $required: $bsd_kit"
    done

    for component in \
      docs \
      modules \
      examples \
      editors
    do
      grep -q \
        "^root/usr/local/share/vitte/$component/" \
        "$listing" ||
        die "BSD installer missing payload component $component: $bsd_kit"
    done

    for command in \
      vitte \
      vittec
    do
      grep -Fx \
        "root/usr/local/bin/$command" \
        "$listing" >/dev/null ||
        die "BSD installer missing $command: $bsd_kit"

      wrapper_text=$(
        tar -xOf \
          "$bsd_kit" \
          "root/usr/local/bin/$command" \
          2>/dev/null ||
          true
      )

      verify_relocatable_wrapper \
        BSD \
        "$command" \
        "$wrapper_text"
    done

    grep -Fx \
      'root/usr/local/bin/vitte-installer-doctor' \
      "$listing" >/dev/null ||
      die "BSD installer missing installer doctor: $bsd_kit"

    rm -f "$listing"
  done
}

verify_portable_kits() {
  for portable_kit in \
    "$OUT_DIR"/vitte-"$VERSION"-portable-*-*.tar.gz
  do
    [ -e "$portable_kit" ] || continue

    printf '[verify-installers] portable %s\n' \
      "$(basename "$portable_kit")"

    verify_sum "$portable_kit"
    verify_manifest_if_present "$portable_kit"

    package_dir=$(basename "$portable_kit" .tar.gz)
    listing=$(mktemp)

    tar -tzf "$portable_kit" > "$listing"

    for required in \
      "$package_dir/bin/vitte" \
      "$package_dir/bin/vittec" \
      "$package_dir/bin/vitte-installer-doctor" \
      "$package_dir/libexec/vitte/vitte" \
      "$package_dir/libexec/vitte/vittec" \
      "$package_dir/share/vitte/tools/install-unix-integrations.sh" \
      "$package_dir/share/vitte/completions/bash/vitte" \
      "$package_dir/share/vitte/completions/fish/vitte.fish" \
      "$package_dir/share/vitte/completions/zsh/_vitte" \
      "$package_dir/README.portable"
    do
      grep -Fx "$required" "$listing" >/dev/null ||
        die "portable archive missing $required: $portable_kit"
    done

    grep -q \
      "^$package_dir/share/vitte/" \
      "$listing" ||
      die "portable archive missing share/vitte payload: $portable_kit"

    for command in \
      vitte \
      vittec
    do
      wrapper_text=$(
        tar -xOzf \
          "$portable_kit" \
          "$package_dir/bin/$command"
      )

      verify_relocatable_wrapper \
        portable \
        "$command" \
        "$wrapper_text"

      printf '%s\n' "$wrapper_text" |
        grep -F \
          'export VITTE_ROOT=${VITTE_ROOT:-$root/share/vitte}' \
          >/dev/null ||
        die "portable $command wrapper does not set relative VITTE_ROOT: $portable_kit"

      printf '%s\n' "$wrapper_text" |
        grep -F \
          "compiler=\"\$root/libexec/vitte/$command\"" \
          >/dev/null ||
        die "portable $command wrapper does not resolve bundled compiler: $portable_kit"
    done

    rm -f "$listing"
  done
}

verify_macos() {
  for arch in \
    arm64 \
    x86_64 \
    universal \
    universal2 \
    macos2006-i386
  do
    for extension in \
      pkg \
      dmg
    do
      file=$OUT_DIR/vitte-${VERSION}-macos-${arch}.${extension}

      [ -e "$file" ] || continue

      printf '[verify-installers] macOS %s %s\n' \
        "$arch" \
        "$extension"

      verify_sum "$file"
      verify_manifest_if_present "$file"
    done
  done
}

verify_solaris() {
  for arch in \
    amd64 \
    i386
  do
    solaris_kit=$OUT_DIR/vitte-${VERSION}-solaris-${arch}-spool.tar.gz

    [ -e "$solaris_kit" ] || continue

    printf '[verify-installers] Solaris %s\n' \
      "$arch"

    verify_sum "$solaris_kit"
    verify_manifest_if_present "$solaris_kit"

    tar -xOzf \
      "$solaris_kit" \
      pkginfo |
      grep -Fx "VITTE_PROCESSOR=$arch" >/dev/null ||
      die "invalid Solaris processor: $solaris_kit"

    for required in \
      pkginfo \
      prototype \
      depend \
      postinstall \
      preremove \
      root/usr/local/share/vitte/INSTALLATION.json \
      root/usr/local/share/vitte/tools/install-unix-integrations.sh \
      install.sh \
      uninstall.sh
    do
      tar -tzf "$solaris_kit" |
        grep -Fx "$required" >/dev/null ||
        die "Solaris kit missing $required: $solaris_kit"
    done

    tar -xOzf \
      "$solaris_kit" \
      prototype |
      grep -F 'usr/local/bin/vitte' >/dev/null ||
      die "Solaris prototype missing vitte command: $solaris_kit"
  done
}

verify_windows() {
  for item in \
    'amd64 0x8664' \
    'i386 0x014c' \
    'arm64 0xaa64' \
    'armv7 0x01c4'
  do
    set -- $item

    arch=$1
    machine=$2

    windows_kit=$OUT_DIR/vitte-${VERSION}-windows-${arch}-nsis.tar.gz

    [ -e "$windows_kit" ] || continue

    printf '[verify-installers] Windows %s\n' \
      "$arch"

    verify_sum "$windows_kit"
    verify_manifest_if_present "$windows_kit"

    tar -xOzf \
      "$windows_kit" \
      BUILD.txt |
      grep -Fx "Processor: $arch (PE machine $machine)" >/dev/null ||
      die "invalid Windows processor manifest: $windows_kit"

    for required in \
      installer.nsi \
      install.ps1 \
      uninstall.ps1 \
      BUILD.txt \
      payload/share/vitte/INSTALLATION.json \
      payload/share/vitte/VERSION \
      payload/share/vitte/tools/install-unix-integrations.sh \
      payload/share/vitte/editors/vim/syntax/vitte.vim \
      payload/share/vitte/completions/bash/vitte \
      payload/share/vitte/completions/fish/vitte.fish \
      payload/share/vitte/completions/zsh/_vitte \
      payload/share/vitte/assets/logo.png
    do
      tar -tzf "$windows_kit" |
        grep -Fx "$required" >/dev/null ||
        die "Windows kit missing $required: $windows_kit"
    done

    if tar -tzf "$windows_kit" |
       grep -Fx 'install.sh' >/dev/null
    then
      die "Windows kit must not contain POSIX install.sh: $windows_kit"
    fi

    nsi=$(
      tar -xOzf \
        "$windows_kit" \
        installer.nsi
    )

    printf '%s\n' "$nsi" |
      grep -F '!include "LogicLib.nsh"' >/dev/null ||
      die "Windows NSIS script missing LogicLib include: $windows_kit"

    printf '%s\n' "$nsi" |
      grep -F '!include "StrFunc.nsh"' >/dev/null ||
      die "Windows NSIS script missing StrFunc include: $windows_kit"

    printf '%s\n' "$nsi" |
      grep -F 'Windows XP through Windows 11' >/dev/null ||
      die "Windows NSIS script missing XP through Windows 11 support label: $windows_kit"

    printf '%s\n' "$nsi" |
      grep -F 'Function un.RemoveFromPath' >/dev/null ||
      die "Windows NSIS script missing PATH uninstall function: $windows_kit"

    printf '%s\n' "$nsi" |
      grep -F \
        'DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "VITTE_ROOT"' \
        >/dev/null ||
      die "Windows NSIS script does not remove VITTE_ROOT: $windows_kit"

    for command in \
      vitte \
      vittec
    do
      for extension in \
        cmd \
        ps1
      do
        tar -tzf "$windows_kit" |
          grep -Fx "payload/bin/$command.$extension" >/dev/null ||
          die "Windows kit missing $extension shim for $command: $windows_kit"
      done

      tar -xOzf \
        "$windows_kit" \
        "payload/bin/$command.cmd" |
        grep -F \
          'set "VITTE_ROOT=%~dp0..\share\vitte"' \
          >/dev/null ||
        die "Windows cmd shim missing VITTE_ROOT for $command: $windows_kit"

      tar -xOzf \
        "$windows_kit" \
        "payload/bin/$command.ps1" |
        grep -F \
          '$env:VITTE_ROOT = Join-Path $ScriptDir "..\share\vitte"' \
          >/dev/null ||
        die "Windows PowerShell shim missing VITTE_ROOT for $command: $windows_kit"
    done

    tar -tzf "$windows_kit" |
      grep -Fx \
        'payload/bin/vitte-installer-doctor.cmd' \
        >/dev/null ||
      die "Windows kit missing cmd installer doctor: $windows_kit"

    tar -xOzf \
      "$windows_kit" \
      payload/bin/vitte-installer-doctor.cmd |
      grep -F \
        'Vitte installer doctor' \
        >/dev/null ||
      die "Windows cmd installer doctor missing status output: $windows_kit"
  done
}

verify_checksums_index() {
  checksum_file=$OUT_DIR/CHECKSUMS.txt

  [ -s "$checksum_file" ] || return 0

  while IFS= read -r line; do
    [ -n "$line" ] || continue

    expected=$(printf '%s\n' "$line" | awk '{print $1}')
    name=$(printf '%s\n' "$line" | awk '{print $2}')

    [ -n "$expected" ] ||
      die "invalid CHECKSUMS.txt entry"

    [ -n "$name" ] ||
      die "invalid CHECKSUMS.txt entry"

    file=$OUT_DIR/$name

    require_file "$file"

    actual=$(
      python3 - "$file" <<'PY'
import hashlib
import sys
from pathlib import Path

path = Path(sys.argv[1])
print(hashlib.sha256(path.read_bytes()).hexdigest())
PY
    )

    [ "$actual" = "$expected" ] ||
      die "CHECKSUMS.txt checksum mismatch: $name"
  done < "$checksum_file"
}

verify_metadata() {
  require_file "$OUT_DIR/INSTALLERS.json"
  require_file "$OUT_DIR/CHECKSUMS.txt"
  require_file "$OUT_DIR/SIGNATURES.json"

  python3 - \
    "$OUT_DIR" \
    "$RELEASE_INSTALLER_GATE" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
release = sys.argv[2] == "1"

installers_path = out / "INSTALLERS.json"
signatures_path = out / "SIGNATURES.json"

installers = json.loads(
    installers_path.read_text(encoding="utf-8")
)

signatures = json.loads(
    signatures_path.read_text(encoding="utf-8")
)

signature_names = {
    item["name"]
    for item in signatures.get("artifacts", [])
    if "name" in item
}

metadata = {
    "INSTALLERS.json",
    "CHECKSUMS.txt",
    "SIGNATURES.json",
    "SBOM.spdx.json",
    "SBOM.cyclonedx.json",
    "ATTESTATION.json",
}

for artifact in installers.get("artifacts", []):
    name = artifact.get("name")

    if not name:
        raise SystemExit(
            "INSTALLERS.json contains artifact without name"
        )

    if (
        name in metadata
        or name.endswith(".sha256")
        or name.endswith(".MANIFEST.json")
    ):
        continue

    artifact_path = out / name

    if not artifact_path.is_file():
        raise SystemExit(
            f"missing artifact: {name}"
        )

    actual_digest = hashlib.sha256(
        artifact_path.read_bytes()
    ).hexdigest()

    declared_digest = artifact.get("sha256")

    if declared_digest != actual_digest:
        raise SystemExit(
            f"INSTALLERS.json checksum mismatch: {name}"
        )

    manifest = out / f"{name}.MANIFEST.json"

    if not manifest.is_file():
        raise SystemExit(
            f"missing artifact manifest: {manifest.name}"
        )

    value = json.loads(
        manifest.read_text(encoding="utf-8")
    )

    for key in (
        "version",
        "os",
        "arch",
        "abi",
        "libc",
        "minimum_version",
        "sha256",
        "installed_commands",
        "contents",
    ):
        if key not in value:
            raise SystemExit(
                f"manifest {manifest.name} missing {key}"
            )

    if value["sha256"] != actual_digest:
        raise SystemExit(
            f"manifest checksum mismatch: {manifest.name}"
        )

    if name not in signature_names:
        raise SystemExit(
            f"SIGNATURES.json missing {name}"
        )

if release:
    for required in (
        "SBOM.spdx.json",
        "SBOM.cyclonedx.json",
        "ATTESTATION.json",
    ):
        if not (out / required).is_file():
            raise SystemExit(
                f"release metadata missing {required}"
            )

    if not signatures.get("all_verified"):
        raise SystemExit(
            "release metadata requires cryptographically verified signatures"
        )
PY
}

scripts_build_require ar
scripts_build_require awk
scripts_build_require bsdtar
scripts_build_require grep
scripts_build_require mktemp
scripts_build_require python3
scripts_build_require tar

for arch in \
  amd64 \
  arm64 \
  armhf \
  armel \
  i386 \
  riscv64 \
  ppc64el \
  s390x \
  mips64el \
  mipsel \
  powerpc \
  sparc64
do
  verify_deb "$arch"
done

for item in \
  'amd64 amd64' \
  'i386 i386' \
  'arm64 aarch64' \
  'armv7 armv7' \
  'armv6 armv6' \
  'riscv64 riscv64' \
  'powerpc powerpc' \
  'powerpc64 powerpc64' \
  'powerpc64le powerpc64le'
do
  set -- $item
  verify_freebsd "$1" "$2"
done

verify_bsd_kits
verify_portable_kits

if [ "$(uname -s)" = Darwin ]; then
  verify_macos
fi

verify_solaris
verify_windows
verify_checksums_index

if [ "$VERIFY_METADATA" -eq 1 ] ||
   [ "$RELEASE_INSTALLER_GATE" -eq 1 ]
then
  verify_metadata
fi

if [ "$STRICT_NATIVE" -eq 1 ]; then
  verify_sum \
    "$OUT_DIR/vitte-${VERSION}-solaris-amd64.pkg"

  verify_sum \
    "$OUT_DIR/vitte-${VERSION}-solaris-i386.pkg"

  verify_sum \
    "$OUT_DIR/vitte-${VERSION}-windows-amd64-installer.exe"

  verify_sum \
    "$OUT_DIR/vitte-${VERSION}-windows-i386-installer.exe"
else
  for arch in \
    amd64 \
    i386
  do
    verify_optional_sum \
      "$OUT_DIR/vitte-${VERSION}-solaris-${arch}.pkg"
  done

  for arch in \
    amd64 \
    i386 \
    arm64 \
    armv7
  do
    verify_optional_sum \
      "$OUT_DIR/vitte-${VERSION}-windows-${arch}-installer.exe"
  done
fi

printf \
  '[verify-installers] OK version=%s strict_native=%s out=%s\n' \
  "$VERSION" \
  "$STRICT_NATIVE" \
  "$OUT_DIR"
