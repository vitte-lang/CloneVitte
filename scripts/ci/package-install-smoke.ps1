param(
  [string]$VitteBin = $(if ($env:VITTE_BIN) { $env:VITTE_BIN } else { "vitte" }),
  [string]$VitteBootstrap = $(if ($env:VITTE_BOOTSTRAP_BIN) { $env:VITTE_BOOTSTRAP_BIN } else { Join-Path $PSScriptRoot "../../build/bin/vitte-bootstrap.exe" }),
  [string]$WorkDir = $env:WORKDIR
)

$ErrorActionPreference = "Stop"
$VitteBin = (Get-Command $VitteBin -ErrorAction Stop).Source
$VitteBootstrap = (Get-Command $VitteBootstrap -ErrorAction Stop).Source
$temporaryWorkDir = [string]::IsNullOrWhiteSpace($WorkDir)
if ($temporaryWorkDir) {
  $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("vitte-package-install-smoke-" + [guid]::NewGuid().ToString("N"))
}
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "modules/json/tests/import_smoke.vit") `
  -Destination (Join-Path $WorkDir "package-smoke.vit")

function Invoke-Vitte {
  param([string[]]$CommandArgs)
  & $VitteBin @CommandArgs
  if ($LASTEXITCODE -ne 0) {
    throw ("Vitte command failed with exit code {0}: {1}" -f $LASTEXITCODE, ($CommandArgs -join ' '))
  }
}

Push-Location $WorkDir
try {
  Invoke-Vitte -CommandArgs @("install", "json")
  $requiredFiles = @(
    ".vitte/modules/json/package.toml",
    ".vitte/modules/json/parser/parser.vit",
    "vitte.packages.toml",
    "vitte.lock"
  )
  foreach ($path in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Package installation did not create $path"
    }
  }
  $manifest = Get-Content -Raw "vitte.packages.toml"
  $lock = Get-Content -Raw "vitte.lock"
  if ($manifest -notmatch '(?m)^dependency\.json = "[0-9]+\.[0-9]+\.[0-9]+"$') {
    throw "Installed package version is missing from vitte.packages.toml"
  }
  if ($lock -notmatch '(?m)^package\.json\.checksum = "sha256:[0-9a-f]{64}"$') {
    throw "Installed package checksum is missing from vitte.lock"
  }

  $env:VITTE_MODULE_PATH = Join-Path $WorkDir ".vitte/modules"
  & $VitteBootstrap "check" "package-smoke.vit"
  if ($LASTEXITCODE -ne 0) { throw "Bootstrap check failed with exit code $LASTEXITCODE" }
  & $VitteBootstrap "compile" "package-smoke.vit" "--emit-c" "-o" "package-smoke.c"
  if ($LASTEXITCODE -ne 0) { throw "Bootstrap compile failed with exit code $LASTEXITCODE" }
  & gcc -std=c17 package-smoke.c -o package-smoke.exe
  if ($LASTEXITCODE -ne 0) { throw "C compiler failed with exit code $LASTEXITCODE" }
  & .\package-smoke.exe
  if ($LASTEXITCODE -ne 0) { throw "Installed package smoke program failed with exit code $LASTEXITCODE" }
  Write-Host "[package-install-smoke] OK bin=$VitteBin workdir=$WorkDir"
} finally {
  Pop-Location
  Remove-Item Env:VITTE_MODULE_PATH -ErrorAction SilentlyContinue
  if ($temporaryWorkDir) {
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
  }
}
