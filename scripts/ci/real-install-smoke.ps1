param(
  [string]$VitteBin = $(if ($env:VITTE_BIN) { $env:VITTE_BIN } else { "vitte" }),
  [string]$CCompiler = $(if ($env:CC) { $env:CC } else { "gcc" }),
  [string]$WorkDir = $env:WORKDIR
)

$ErrorActionPreference = "Stop"
$vitteCommand = Get-Command $VitteBin -ErrorAction Stop
$VitteBin = $vitteCommand.Source
$installRoot = Split-Path -Parent (Split-Path -Parent $VitteBin)
$integrationRoot = Join-Path $installRoot "share/vitte"
foreach ($relative in @(
  "editors/vim/syntax/vitte.vim",
  "editors/emacs/vitte-mode.el",
  "editors/nano/vitte.nanorc",
  "editors/geany/filetypes.vitte.conf",
  "completions/bash/vitte",
  "completions/fish/vitte.fish",
  "completions/zsh/_vitte"
)) {
  $integration = Join-Path $integrationRoot $relative
  if (-not (Test-Path -LiteralPath $integration -PathType Leaf) -or
      (Get-Item -LiteralPath $integration).Length -eq 0) {
    throw "Missing installed integration: $integration"
  }
}
$ccCommand = Get-Command $CCompiler -ErrorAction Stop
$CCompiler = $ccCommand.Source
$temporaryWorkDir = [string]::IsNullOrWhiteSpace($WorkDir)
if ($temporaryWorkDir) {
  $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("vitte-real-install-smoke-" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

function Invoke-Vitte {
  param([string[]]$CommandArgs)
  & $VitteBin @CommandArgs
  if ($LASTEXITCODE -ne 0) {
    throw ("Vitte command failed with exit code {0}: {1}" -f $LASTEXITCODE, ($CommandArgs -join ' '))
  }
}

$source = @"
proc main() -> int {
  return 0;
}
"@
$smoke = Join-Path $WorkDir "smoke.vit"
Set-Content -Path $smoke -Value $source -Encoding ASCII

Push-Location $WorkDir
try {
  # Required post-install contract:
  $doctor = Join-Path (Split-Path -Parent $VitteBin) "vitte-installer-doctor.cmd"
  if (Test-Path $doctor) {
    & $doctor
    if ($LASTEXITCODE -ne 0) { throw "Installer doctor failed with exit code $LASTEXITCODE" }
  }
  Invoke-Vitte -CommandArgs @("--version")
  Invoke-Vitte -CommandArgs @("--help")
  Invoke-Vitte -CommandArgs @("check", "smoke.vit")
  # Windows host-tool invocation is not available in the compiler shim yet.
  # Exercise the installed compiler through C emission and the runner's GCC.
  Invoke-Vitte -CommandArgs @("compile", "smoke.vit", "--emit-c", "-o", "smoke.c")
  & $CCompiler -std=c17 smoke.c -o smoke.exe
  if ($LASTEXITCODE -ne 0) {
    throw "C compiler failed with exit code $LASTEXITCODE"
  }
  $smokeProgram = ".\smoke.exe"
  if (-not (Test-Path $smokeProgram)) { throw "Vitte smoke executable was not produced in $WorkDir" }
  & $smokeProgram
  if ($LASTEXITCODE -ne 0) { throw "Vitte smoke program failed with exit code $LASTEXITCODE" }
  Write-Host "[real-install-smoke] OK bin=$VitteBin workdir=$WorkDir"
} finally {
  Pop-Location
  if ($temporaryWorkDir) {
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
  }
}
