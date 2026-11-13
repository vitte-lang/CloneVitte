[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
  [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
  throw 'LOCALAPPDATA is not defined; run this from a Windows user session.'
}

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sourceBinary = Join-Path $repositoryRoot 'build\bin\vitte.exe'
$moduleSource = Join-Path $repositoryRoot 'modules\json'
$moduleMakefile = Join-Path $moduleSource 'Makefile'
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\Vitte'
$binaryDirectory = Join-Path $installRoot 'bin'
$installedBinary = Join-Path $binaryDirectory 'vitte.exe'
$moduleDirectory = Join-Path $env:LOCALAPPDATA 'Vitte\modules\json'

if (-not (Test-Path -LiteralPath $moduleMakefile -PathType Leaf)) {
  throw "JSON module Makefile not found: $moduleMakefile"
}

$makeCommand = Get-Command 'make.exe' -ErrorAction SilentlyContinue
if ($null -eq $makeCommand) {
  throw 'MSYS2 or Cygwin make.exe is required; run this target from that environment.'
}

$cygpathCommand = Get-Command 'cygpath.exe' -ErrorAction SilentlyContinue
if ($null -eq $cygpathCommand) {
  throw 'MSYS2 or Cygwin cygpath.exe is required; run this target from that environment.'
}

$moduleSourceMsys = [string](& $cygpathCommand.Source -u $moduleSource)
$moduleSourceExitCode = $LASTEXITCODE
$moduleDirectoryMsys = [string](& $cygpathCommand.Source -u $moduleDirectory)
$moduleDirectoryExitCode = $LASTEXITCODE
if ($moduleSourceExitCode -ne 0 -or
    $moduleDirectoryExitCode -ne 0 -or
    [string]::IsNullOrWhiteSpace($moduleSourceMsys) -or
    [string]::IsNullOrWhiteSpace($moduleDirectoryMsys)) {
  throw 'MSYS2 could not convert the JSON module paths.'
}

function Invoke-JsonModuleMake {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('install', 'uninstall')]
    [string]$Target
  )

  & $makeCommand.Source -C $moduleSourceMsys $Target "MODULE_DIR=$moduleDirectoryMsys"
  if ($LASTEXITCODE -ne 0) {
    throw "JSON module $Target failed with exit code $LASTEXITCODE."
  }
}

function Get-UserPathEntries {
  $userPath = [Environment]::GetEnvironmentVariable(
    'Path',
    [EnvironmentVariableTarget]::User)
  if ([string]::IsNullOrWhiteSpace($userPath)) {
    return @()
  }

  return @(
    $userPath -split ';' |
      Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-PathKey {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $expandedPath = [Environment]::ExpandEnvironmentVariables(
    $Path.Trim().Trim('"'))
  return $expandedPath.Replace('/', '\').TrimEnd([char[]]@('\', '/'))
}

if (-not $Uninstall -and
    -not (Test-Path -LiteralPath $sourceBinary -PathType Leaf)) {
  throw "Built Vitte executable not found: $sourceBinary. Run make first."
}

$action = if ($Uninstall) {
  'Remove the current-user Vitte installation'
} else {
  'Install Vitte for the current user'
}

if (-not $PSCmdlet.ShouldProcess($installRoot, $action)) {
  return
}

if ($Uninstall) {
  Invoke-JsonModuleMake -Target 'uninstall'

  if (Test-Path -LiteralPath $installedBinary -PathType Leaf) {
    Remove-Item -LiteralPath $installedBinary -Force
  }
  if ((Test-Path -LiteralPath $binaryDirectory -PathType Container) -and
      [System.IO.Directory]::GetFileSystemEntries($binaryDirectory).Length -eq 0) {
    Remove-Item -LiteralPath $binaryDirectory
  }

  $pathEntries = @(Get-UserPathEntries)
  $binaryPathKey = Get-PathKey $binaryDirectory
  $remainingPathEntries = @(
    $pathEntries | Where-Object {
      (Get-PathKey $_) -ine $binaryPathKey
    })
  if ($remainingPathEntries.Count -ne $pathEntries.Count) {
    $newUserPath = $remainingPathEntries -join ';'
    if ($remainingPathEntries.Count -eq 0) {
      $newUserPath = $null
    }
    [Environment]::SetEnvironmentVariable(
      'Path',
      $newUserPath,
      [EnvironmentVariableTarget]::User)
  }

  Write-Host 'Vitte was removed for the current user. Reopen terminals to refresh PATH.'
  return
}

Invoke-JsonModuleMake -Target 'install'
New-Item -ItemType Directory -Force -Path $binaryDirectory | Out-Null
Copy-Item -LiteralPath $sourceBinary -Destination $installedBinary -Force

$pathEntries = @(Get-UserPathEntries)
$binaryPathKey = Get-PathKey $binaryDirectory
$hasBinaryPath = $false
foreach ($entry in $pathEntries) {
  if ((Get-PathKey $entry) -ieq $binaryPathKey) {
    $hasBinaryPath = $true
    break
  }
}

if (-not $hasBinaryPath) {
  $pathEntries += $binaryDirectory
  [Environment]::SetEnvironmentVariable(
    'Path',
    ($pathEntries -join ';'),
    [EnvironmentVariableTarget]::User)
}

Write-Host "Installed Vitte: $installedBinary"
Write-Host "Installed JSON module: $moduleDirectory"
Write-Host 'Reopen terminals to use vitte from PATH.'
