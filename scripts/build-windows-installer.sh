#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT_NAME=build-windows-installer
. "$ROOT_DIR/scripts/common.sh"
scripts_build_parse_common_flags "$@"
VERSION=${VERSION:-$(scripts_build_package_version)}
OUT_DIR=${OUT_DIR:-$ROOT_DIR/pkgout}
case "$OUT_DIR" in /*) ;; *) OUT_DIR=$ROOT_DIR/$OUT_DIR ;; esac
ARCH=${ARCH:-all}
PACKAGE_NAME=${PACKAGE_NAME:-vitte}
SIGN=${SIGN:-0}
REQUIRE_NATIVE=${STRICT_NATIVE:-${RELEASE_INSTALLER_GATE:-0}}
WINDOWS_SIGNTOOL=${WINDOWS_SIGNTOOL:-signtool}
WINDOWS_SIGN_CERT=${WINDOWS_SIGN_CERT:-}
WINDOWS_VITTE_EXE=${WINDOWS_VITTE_EXE:-}
WINDOWS_TARGETS=${WINDOWS_TARGETS:-xp vista 7 8 8.1 10 11}
WINDOWS_MINIMUM_VERSION=${WINDOWS_MINIMUM_VERSION:-$(scripts_build_minimum_version_for_target windows)}
scripts_build_maybe_help "usage: build-windows-installer.sh [--dry-run]"
scripts_build_maybe_dry_run "would build Windows NSIS kits version=$VERSION arch=$ARCH out=$OUT_DIR"

die() {
  printf '[build-windows-installer][error] %s\n' "$*" >&2
  exit 1
}

require() {
  command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"
}

machine_for_arch() {
  case "$1" in
    amd64) printf '0x8664' ;;
    i386) printf '0x014c' ;;
    arm64) printf '0xaa64' ;;
    armv7) printf '0x01c4' ;;
    *) die "unsupported Windows architecture: $1" ;;
  esac
}

validate_pe() {
  python3 - "$1" "$2" <<'PY'
import struct
import sys
from pathlib import Path

expected = {
    "amd64": 0x8664,
    "i386": 0x014C,
    "arm64": 0xAA64,
    "armv7": 0x01C4,
}[sys.argv[2]]
path = Path(sys.argv[1])
data = path.read_bytes()
if len(data) < 64 or data[:2] != b"MZ":
    raise SystemExit(f"not a PE executable: {path}")
offset = struct.unpack_from("<I", data, 0x3C)[0]
if offset + 6 > len(data) or data[offset:offset + 4] != b"PE\0\0":
    raise SystemExit(f"invalid PE signature: {path}")
machine = struct.unpack_from("<H", data, offset + 4)[0]
if machine != expected:
    raise SystemExit(f"wrong PE processor 0x{machine:04x}, expected 0x{expected:04x}: {path}")
PY
}

sign_windows_artifact() {
  artifact=$1
  [ "$SIGN" -eq 1 ] || return 0
  [ -n "$WINDOWS_SIGN_CERT" ] ||
    die "SIGN=1 requires WINDOWS_SIGN_CERT"
  command -v "$WINDOWS_SIGNTOOL" >/dev/null 2>&1 ||
    die "SIGN=1 requires signtool or WINDOWS_SIGNTOOL"
  "$WINDOWS_SIGNTOOL" sign /fd SHA256 /f "$WINDOWS_SIGN_CERT" "$artifact"
}

find_windows_exe() {
  arch=$1
  [ -z "$WINDOWS_VITTE_EXE" ] || {
    printf '%s\n' "$WINDOWS_VITTE_EXE"
    return 0
  }
  for candidate in \
    "$ROOT_DIR/target/windows-$arch/vitte.exe" \
    "$ROOT_DIR/target/windows/$arch/vitte.exe" \
    "$ROOT_DIR/bin/windows-$arch/vitte.exe" \
    "$ROOT_DIR/bin/$arch/vitte.exe"
  do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

write_cmd_shims() {
  payload=$1
  for command in vitte vittec; do
    cat > "$payload/bin/$command.cmd" <<EOF
@echo off
setlocal
set "VITTE_ROOT=%~dp0..\share\vitte"
if exist "%~dp0$command.exe" (
  "%~dp0$command.exe" %*
) else (
  "%~dp0vitte.exe" %*
)
exit /b %ERRORLEVEL%
EOF
    cat > "$payload/bin/$command.ps1" <<EOF
\$ErrorActionPreference = "Stop"
\$ScriptDir = Split-Path -Parent \$MyInvocation.MyCommand.Path
\$env:VITTE_ROOT = Join-Path \$ScriptDir "..\\share\\vitte"
\$command = Join-Path \$ScriptDir "$command.exe"
if (-not (Test-Path \$command)) {
  \$command = Join-Path \$ScriptDir "vitte.exe"
}
& \$command @args
exit \$LASTEXITCODE
EOF
  done
}

write_windows_doctor_shim() {
  payload=$1
  cat > "$payload/bin/vitte-installer-doctor.cmd" <<'EOF'
@echo off
setlocal
set "VITTE_BIN=%~dp0"
set "VITTE_ROOT=%~dp0..\share\vitte"
set "VITTE_STATUS=0"
echo Vitte installer doctor
echo prefix: %~dp0..
echo bin: %VITTE_BIN%
echo VITTE_ROOT: %VITTE_ROOT%
if exist "%VITTE_BIN%vitte.exe" (echo OK   compiler exists) else (echo FAIL missing compiler & set "VITTE_STATUS=1")
if exist "%VITTE_BIN%vittec.exe" (echo OK   vittec exists) else (echo FAIL missing vittec & set "VITTE_STATUS=1")
if exist "%VITTE_ROOT%\VERSION" (echo OK   version file exists) else (echo FAIL missing version file & set "VITTE_STATUS=1")
if exist "%VITTE_ROOT%\INSTALLATION.json" (echo OK   installation manifest exists) else (echo FAIL missing installation manifest & set "VITTE_STATUS=1")
if exist "%VITTE_BIN%vitte.exe" "%VITTE_BIN%vitte.exe" --version
if errorlevel 1 set "VITTE_STATUS=1"
exit /b %VITTE_STATUS%
EOF
}

write_powershell_installers() {
  stage=$1
  cat > "$stage/install.ps1" <<'EOF'
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DefaultPrefix = Join-Path $env:ProgramFiles "Vitte"
if ([string]::IsNullOrEmpty($DefaultPrefix)) {
  $DefaultPrefix = "C:\Vitte"
}
$Prefix = $DefaultPrefix
if ($args.Count -ge 1 -and -not [string]::IsNullOrEmpty($args[0])) {
  $Prefix = $args[0]
}
$Payload = Join-Path $ScriptDir "payload"
if (-not (Test-Path $Payload)) {
  throw "missing payload directory: $Payload"
}
if (-not (Test-Path $Prefix)) {
  New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
}
Copy-Item -Path (Join-Path $Payload "*") -Destination $Prefix -Recurse -Force
$BinDir = Join-Path $Prefix "bin"
$ShareDir = Join-Path $Prefix "share\vitte"
[Environment]::SetEnvironmentVariable("VITTE_ROOT", $ShareDir, "Machine")
$MachinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($MachinePath -eq $null) { $MachinePath = "" }
if ((";" + $MachinePath + ";").IndexOf(";" + $BinDir + ";", [StringComparison]::OrdinalIgnoreCase) -lt 0) {
  if ($MachinePath.Length -eq 0) {
    [Environment]::SetEnvironmentVariable("Path", $BinDir, "Machine")
  } else {
    [Environment]::SetEnvironmentVariable("Path", $BinDir + ";" + $MachinePath, "Machine")
  }
}
Write-Host "Vitte installed to $Prefix"
Write-Host "Supported Windows: XP, Vista, 7, 8, 8.1, 10, 11"
EOF

  cat > "$stage/uninstall.ps1" <<'EOF'
$ErrorActionPreference = "Stop"
$DefaultPrefix = Join-Path $env:ProgramFiles "Vitte"
if ([string]::IsNullOrEmpty($DefaultPrefix)) {
  $DefaultPrefix = "C:\Vitte"
}
$Prefix = $DefaultPrefix
if ($args.Count -ge 1 -and -not [string]::IsNullOrEmpty($args[0])) {
  $Prefix = $args[0]
}
$BinDir = Join-Path $Prefix "bin"
$MachinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($MachinePath -ne $null) {
  $parts = @()
  foreach ($part in $MachinePath.Split(";")) {
    if ($part.Length -gt 0 -and [String]::Compare($part, $BinDir, $true) -ne 0) {
      $parts += $part
    }
  }
  [Environment]::SetEnvironmentVariable("Path", [String]::Join(";", $parts), "Machine")
}
[Environment]::SetEnvironmentVariable("VITTE_ROOT", $null, "Machine")
if (Test-Path $Prefix) {
  Remove-Item -Recurse -Force $Prefix
}
Write-Host "Vitte removed from $Prefix"
EOF
}

write_nsi() {
  stage=$1
  arch=$2
  cat > "$stage/installer.nsi" <<'NSI'
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "LogicLib.nsh"
!include "StrFunc.nsh"

${StrRep}
${UnStrRep}

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "vitte-windows-installer.exe"
!endif
!ifndef ARCH
  !define ARCH "amd64"
!endif
!ifndef WINDOWS_TARGETS
  !define WINDOWS_TARGETS "xp vista 7 8 8.1 10 11"
!endif

Name "Vitte ${VERSION} (${ARCH})"
OutFile "${OUT_FILE}"
BrandingText "Vitte ${VERSION} | Compiler, runtime, editors and completions"
ShowInstDetails show
ShowUninstDetails show
InstallDir "$PROGRAMFILES\Vitte"
!if "${ARCH}" == "amd64"
  InstallDir "$PROGRAMFILES64\Vitte"
!endif
InstallDirRegKey HKLM "Software\Vitte" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install Vitte ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "The Vitte installer sets up the compiler, runtime, standard library, editor integrations, and shell completions.$\r$\n$\r$\nThe next pages show the license, installation options, and destination."
!define MUI_FINISHPAGE_TITLE "Vitte ${VERSION} is ready"
!define MUI_FINISHPAGE_TEXT "Open a new terminal and run vitte --version, then vitte-installer-doctor to check the installation."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

Function AddToPath
  Push $0
  Push $1
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  StrCpy $1 "$INSTDIR\bin"
  ${If} $0 == ""
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$1"
  ${Else}
    StrCmp $0 "$1" done
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$1;$0"
  ${EndIf}
done:
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "VITTE_ROOT" "$INSTDIR\share\vitte"
  Pop $1
  Pop $0
FunctionEnd

Function un.RemoveFromPath
  Push $0
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  ${UnStrRep} $0 $0 "$INSTDIR\bin;" ""
  ${UnStrRep} $0 $0 ";$INSTDIR\bin" ""
  ${UnStrRep} $0 $0 "$INSTDIR\bin" ""
  WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
  DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "VITTE_ROOT"
  Pop $0
FunctionEnd

Section "Vitte" SEC_VITTE
  SectionIn RO
  ${IfNot} ${AtLeastWinXP}
    MessageBox MB_ICONSTOP "Vitte requires Windows XP or later."
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "payload\*"
  Call AddToPath
  CreateDirectory "$SMPROGRAMS\Vitte"
  CreateShortCut "$SMPROGRAMS\Vitte\Vitte shell.lnk" "$SYSDIR\cmd.exe" '/K ""$INSTDIR\bin\vitte.cmd" --help"'
  CreateShortCut "$SMPROGRAMS\Vitte\Uninstall Vitte.lnk" "$INSTDIR\uninstall.exe"
  WriteRegStr HKCR ".vit" "" "Vitte.Source"
  WriteRegStr HKCR "Vitte.Source" "" "Vitte source file"
  WriteRegStr HKCR "Vitte.Source\shell\check\command" "" '"$INSTDIR\bin\vitte.cmd" check "%1"'
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Vitte" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Vitte" "Architecture" "${ARCH}"
  WriteRegStr HKLM "Software\Vitte" "SupportedWindows" "${WINDOWS_TARGETS}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte" "DisplayName" "Vitte ${VERSION} (${ARCH})"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte" "Publisher" "Vitte Team"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte" "UninstallString" '"$INSTDIR\uninstall.exe"'
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortCut "$DESKTOP\Vitte shell.lnk" "$SYSDIR\cmd.exe" '/K ""$INSTDIR\bin\vitte.cmd" --help"'
SectionEnd

Section "Uninstall"
  Call un.RemoveFromPath
  Delete "$DESKTOP\Vitte shell.lnk"
  RMDir /r "$SMPROGRAMS\Vitte"
  DeleteRegKey HKCR "Vitte.Source"
  DeleteRegKey HKCR ".vit"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitte"
  DeleteRegKey HKLM "Software\Vitte"
  RMDir /r "$INSTDIR"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_VITTE} "Compiler, runtime, standard library, editor integrations, shell completions, and installation doctor."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "Add a shortcut that opens a Vitte command shell."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Vitte"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Vitte ${ARCH} installer for Windows XP through Windows 11"
NSI
}

build_one() {
  arch=$1
  stage=$ROOT_DIR/target/installer-windows-$arch
  payload=$stage/payload
  package_file=$OUT_DIR/${PACKAGE_NAME}-${VERSION}-windows-${arch}-installer.exe
  kit_file=$OUT_DIR/${PACKAGE_NAME}-${VERSION}-windows-${arch}-nsis.tar.gz
  machine=$(machine_for_arch "$arch")

  rm -rf "$stage"
  mkdir -p "$payload/bin" "$payload/share/vitte" "$OUT_DIR"

  VERSION=$VERSION "$ROOT_DIR/scripts/stage-installer-payload.sh" "$payload" windows "$arch" windows
  scripts_build_verify_modules "$payload/share/vitte"
  [ ! -f "$ROOT_DIR/LICENSE" ] || install -m 0644 "$ROOT_DIR/LICENSE" "$stage/LICENSE"
  [ ! -f "$ROOT_DIR/README.md" ] || install -m 0644 "$ROOT_DIR/README.md" "$stage/README.md"
  [ ! -f "$ROOT_DIR/assets/logo.png" ] || install -m 0644 "$ROOT_DIR/assets/logo.png" "$stage/logo.png"

  has_pe=0
  if exe=$(find_windows_exe "$arch"); then
    validate_pe "$exe" "$arch" || die "invalid Windows $arch compiler payload"
    install -m 0755 "$exe" "$payload/bin/vitte.exe"
    has_pe=1
  fi

  write_cmd_shims "$payload"
  write_windows_doctor_shim "$payload"
  write_powershell_installers "$stage"
  write_nsi "$stage" "$arch"

  cat > "$stage/BUILD.txt" <<EOF
Vitte Windows $arch NSIS build kit
Version: $VERSION
Processor: $arch (PE machine $machine)
Supported Windows: $WINDOWS_TARGETS
Installer runtime target: Windows XP through Windows 11

To create a native EXE installer:
  1. Put a valid $arch PE compiler at payload/bin/vitte.exe.
  2. Run:
       makensis -DVERSION=$VERSION -DARCH=$arch -DWINDOWS_TARGETS="$WINDOWS_TARGETS" -DOUT_FILE=${PACKAGE_NAME}-${VERSION}-windows-${arch}-installer.exe installer.nsi

The canonical generator validates the PE header before invoking NSIS:
  ARCH=$arch WINDOWS_VITTE_EXE=/path/to/vitte.exe scripts/build-windows-installer.sh
EOF

  rm -f "$kit_file" "$kit_file.sha256"
  scripts_build_tar_gz "$kit_file" "$stage" installer.nsi install.ps1 uninstall.ps1 BUILD.txt LICENSE README.md logo.png payload
  scripts_build_sha256_write "$kit_file" "$kit_file.sha256"
  scripts_build_write_artifact_manifest \
    "$kit_file" windows "$arch" "$VERSION" \
    "$kit_file.MANIFEST.json" "$WINDOWS_MINIMUM_VERSION"
  printf '[build-windows-installer] wrote build kit %s (%s bytes)\n' "$kit_file" "$(wc -c < "$kit_file" | tr -d ' ')"

  if [ "$has_pe" -eq 1 ] && command -v makensis >/dev/null 2>&1; then
    rm -f "$package_file" "$package_file.sha256"
    (cd "$stage" && makensis -DVERSION="$VERSION" -DARCH="$arch" -DWINDOWS_TARGETS="$WINDOWS_TARGETS" -DOUT_FILE="$package_file" installer.nsi)
    validate_pe "$package_file" "$arch" || die "NSIS output is not a Windows $arch executable"
    sign_windows_artifact "$package_file"
    scripts_build_sha256_write "$package_file" "$package_file.sha256"
    scripts_build_write_artifact_manifest \
      "$package_file" windows "$arch" "$VERSION" \
      "$package_file.MANIFEST.json" "$WINDOWS_MINIMUM_VERSION"
    printf '[build-windows-installer] wrote %s (%s bytes)\n' "$package_file" "$(wc -c < "$package_file" | tr -d ' ')"
  elif [ "$has_pe" -eq 0 ]; then
    [ "$REQUIRE_NATIVE" -eq 0 ] ||
      die "release requires a real Windows $arch PE payload; set WINDOWS_VITTE_EXE or the architecture-specific VITTE_BIN override"
    printf '[build-windows-installer] Windows %s PE payload unavailable; NSIS kit generated, native .exe deferred\n' "$arch" >&2
  else
    [ "$REQUIRE_NATIVE" -eq 0 ] ||
      die "release requires makensis to produce the Windows $arch installer"
    printf '[build-windows-installer] makensis unavailable; NSIS kit generated, native .exe deferred\n' >&2
  fi
}

for tool in install python3 tar wc; do
  require "$tool"
done

[ -x "$ROOT_DIR/scripts/stage-installer-payload.sh" ] ||
  die "payload staging script is missing or not executable"

case "$ARCH" in
  all)
    arches='i386 amd64 arm64 armv7'
    ;;
  x86_64 | X86_64 | amd64 | AMD64 | x64)
    arches=amd64
    ;;
  i386 | i486 | i586 | i686 | x86)
    arches=i386
    ;;
  aarch64 | AArch64 | AARCH64 | arm64 | ARM64)
    arches=arm64
    ;;
  armv7 | arm)
    arches=armv7
    ;;
  *)
    die "unsupported Windows architecture: $ARCH"
    ;;
esac

for arch in $arches; do
  build_one "$arch"
done

printf '[build-windows-installer] complete version=%s arch=%s out=%s targets=%s\n' \
  "$VERSION" "$ARCH" "$OUT_DIR" "$WINDOWS_TARGETS"
