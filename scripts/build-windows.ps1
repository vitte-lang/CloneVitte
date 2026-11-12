#requires -Version 5.1
<#+
.SYNOPSIS
  Builds Vitte Windows installer packages through build-windows-installer.sh.
.DESCRIPTION
  Windows-native PowerShell entrypoint. Validates inputs and tool availability,
  translates paths for Git Bash/MSYS2, forwards a scoped environment, and
  propagates the builder exit code. It does not silently install toolchains.
.EXAMPLE
  .\scripts\build-windows-installer.ps1 -Arch amd64 -WindowsTargets '10 11' -Full -Verify
.EXAMPLE
  .\scripts\build-windows-installer.ps1 -ListTargets
#>
[CmdletBinding()]
param(
    [string]$Arch = $(if ($env:ARCH) { $env:ARCH } else { 'all' }),
    [string]$Version = $env:VERSION,
    [string]$OutDir = $env:OUT_DIR,
    [Alias('Name')][string]$PackageName = $(if ($env:PACKAGE_NAME) { $env:PACKAGE_NAME } else { 'vitte' }),
    [string]$WindowsTargets = $(if ($env:WINDOWS_TARGETS) { $env:WINDOWS_TARGETS } else { 'xp vista 7 8 8.1 10 11' }),
    [Alias('VitteBin','WindowsVitte')][string]$WindowsVitteExe = $env:WINDOWS_VITTE_EXE,
    [Alias('Signtool')][string]$WindowsSigntool = $(if ($env:WINDOWS_SIGNTOOL) { $env:WINDOWS_SIGNTOOL } else { 'signtool' }),
    [Alias('SignCertificate')][string]$WindowsSignCert = $env:WINDOWS_SIGN_CERT,
    [switch]$Sign,
    [switch]$StrictNative,
    [switch]$Full,
    [switch]$Bootstrap,
    [switch]$Verify,
    [switch]$Reproducible,
    [switch]$DryRun,
    [switch]$ListTargets,
    [switch]$PrintEnv,
    [switch]$Help,
    [string]$Shell = $env:VITTE_SH,
    [string]$BuilderPath,
    [switch]$SkipToolChecks
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw "[vitte-windows] $Message" }
function Find-Executable([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
        $cmd = Get-Command -Name $candidate -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $cmd) { return $cmd.Source }
    }
    return $null
}
function Convert-ToPosix([string]$Value, [bool]$MustExist = $false) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $Value }
    if ($MustExist -and -not (Test-Path -LiteralPath $Value)) { Fail "Path does not exist: $Value" }
    if (Test-Path -LiteralPath $Value) {
        $Value = (Resolve-Path -LiteralPath $Value).ProviderPath
    } elseif (-not [System.IO.Path]::IsPathRooted($Value)) {
        $Value = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).ProviderPath $Value))
    }
    if ($script:cygpath) {
        $result = & $script:cygpath -u -- $Value
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($result -join ''))) {
            Fail "cygpath failed for $Value"
        }
        return ($result -join '').Trim()
    }
    # Git Bash/MSYS2 uses /c/... for C:\... paths. UNC needs cygpath.
    if ($Value -match '^[\\]{2}') { Fail 'UNC paths require cygpath.exe' }
    if ($Value -match '^([A-Za-z]):[\\/](.*)$') {
        return '/' + $Matches[1].ToLowerInvariant() + '/' + ($Matches[2] -replace '\\','/')
    }
    return ($Value -replace '\\','/')
}
function Quote-Posix([string]$Value) {
    return "'" + $Value.Replace("'", "'\"'\"'") + "'"
}
function Set-ScopedEnvironment([string]$Key, [AllowNull()][string]$Value) {
    if (-not $script:previousEnv.ContainsKey($Key)) {
        $script:previousEnv[$Key] = [Environment]::GetEnvironmentVariable($Key, 'Process')
    }
    [Environment]::SetEnvironmentVariable($Key, $Value, 'Process')
}
function Restore-Environment {
    foreach ($key in $script:previousEnv.Keys) {
        [Environment]::SetEnvironmentVariable($key, $script:previousEnv[$key], 'Process')
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path -LiteralPath (Join-Path $scriptDir '..')).ProviderPath
if ([string]::IsNullOrWhiteSpace($BuilderPath)) {
    $BuilderPath = Join-Path $scriptDir 'build-windows-installer.sh'
}
