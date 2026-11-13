[CmdletBinding()]
param(
    [string]$Arch = 'amd64',
    [string]$Version = '',
    [string]$OutDir = '',
    [string]$PackageName = 'vitte',
    [string]$WindowsTargets = 'xp vista 7 8 8.1 10 11',
    [Alias('VitteBin')]
    [string]$VitteExe = '',
    [Alias('WindowsSignCert')]
    [string]$SignCert = '',
    [string]$SignTool = 'signtool',
    [string]$BuilderPath = '',
    [string]$Shell = '',
    [switch]$DryRun,
    [switch]$ListTargets,
    [switch]$PrintEnv,
    [switch]$Sign,
    [switch]$StrictNative,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Show-Usage {
    @'
Usage: .\build-windows.ps1 [options]

Build the Windows installer and portable ZIP through build-windows-installer.sh.
Requires Bash (Git Bash or MSYS2); NSIS is required for executable installers.

  -Arch ARCH             target architecture (amd64, arm64, i386; default amd64)
  -Version VERSION       package version override
  -OutDir DIR            output directory
  -PackageName NAME      package name (default vitte)
  -WindowsTargets LIST   Windows compatibility targets
  -VitteExe PATH         compiler executable to package
  -Sign -SignCert PATH   Authenticode-sign output using this certificate
  -SignTool PATH         signtool executable (default signtool)
  -BuilderPath PATH      alternate build-windows-installer.sh
  -Shell PATH            Bash executable (default: discover PATH, MSYS2 or Git Bash)
  -StrictNative          require native artifacts for every requested target
  -DryRun                print the build plan without building
  -ListTargets           list supported architectures
  -PrintEnv              print the effective wrapper configuration
  -Help                  show this help
'@ | Write-Output
}

function Stop-WithError([string]$Message) {
    throw "build-windows.ps1: $Message"
}

function Find-Application([string[]]$Names, [switch]$Optional) {
    foreach ($name in $Names) {
        if ([string]::IsNullOrWhiteSpace($name)) {
            continue
        }
        if (Test-Path -LiteralPath $name -PathType Leaf) {
            return (Resolve-Path -LiteralPath $name).Path
        }
        $command = Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    if (-not $Optional) {
        Stop-WithError "required executable not found: $($Names -join ', ')"
    }
    return ''
}

function Convert-ToPosixPath([string]$Path, [string]$Cygpath) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ''
    }

    $resolvedPath = $Path
    if (-not [IO.Path]::IsPathRooted($resolvedPath)) {
        $resolvedPath = [IO.Path]::GetFullPath($resolvedPath)
    }
    if (Test-Path -LiteralPath $resolvedPath) {
        $resolvedPath = (Resolve-Path -LiteralPath $resolvedPath).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($Cygpath)) {
        $converted = & $Cygpath -u -- $resolvedPath
        if ($LASTEXITCODE -ne 0) {
            Stop-WithError "cygpath could not convert: $resolvedPath"
        }
        return ($converted | Out-String).Trim()
    }

    if ($resolvedPath -match '^(?<drive>[A-Za-z]):[\\/](?<tail>.*)$') {
        $drive = $Matches.drive.ToLowerInvariant()
        $tail = $Matches.tail -replace '\\', '/'
        return "/$drive/$tail"
    }
    if ($resolvedPath.StartsWith('/')) {
        return ($resolvedPath -replace '\\', '/')
    }
    Stop-WithError "cannot convert path for Bash; install cygpath or use a drive-qualified path: $resolvedPath"
}

if ($Help) {
    Show-Usage
    exit 0
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($BuilderPath)) {
    $BuilderPath = Join-Path $scriptDirectory 'build-windows-installer.sh'
}
if (-not (Test-Path -LiteralPath $BuilderPath -PathType Leaf)) {
    Stop-WithError "builder script not found: $BuilderPath"
}
$BuilderPath = (Resolve-Path -LiteralPath $BuilderPath).Path

if ($PrintEnv) {
    @(
        "ARCH=$Arch"
        "VERSION=$Version"
        "OUT_DIR=$OutDir"
        "PACKAGE_NAME=$PackageName"
        "WINDOWS_TARGETS=$WindowsTargets"
        "WINDOWS_VITTE_EXE=$VitteExe"
        "WINDOWS_SIGN_CERT=$SignCert"
        "WINDOWS_SIGNTOOL=$SignTool"
        "SIGN=$([int]$Sign.IsPresent)"
        "STRICT_NATIVE=$([int]$StrictNative.IsPresent)"
        "BUILDER_PATH=$BuilderPath"
    ) | Write-Output
    exit 0
}

$bashCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($Shell)) {
    $bashCandidates += $Shell
}
if (-not [string]::IsNullOrWhiteSpace($env:VITTE_BASH)) {
    $bashCandidates += $env:VITTE_BASH
}
$knownBashCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:MSYS2_ROOT)) {
    $knownBashCandidates += Join-Path $env:MSYS2_ROOT 'usr\bin\bash.exe'
}
if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
    $knownBashCandidates += Join-Path $env:SystemDrive 'msys64\usr\bin\bash.exe'
}
if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $knownBashCandidates += Join-Path $env:ProgramFiles 'Git\bin\bash.exe'
}
if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    $knownBashCandidates += Join-Path $env:LOCALAPPDATA 'Programs\Git\bin\bash.exe'
}
$bashCandidates += $knownBashCandidates
$bashCandidates += @('bash.exe', 'bash')
$bash = Find-Application -Names $bashCandidates
$bashDirectory = Split-Path -Parent $bash
$cygpath = Find-Application -Names @(
    (Join-Path $bashDirectory 'cygpath.exe'),
    'cygpath.exe',
    'cygpath'
) -Optional

$posixBuilder = Convert-ToPosixPath -Path $BuilderPath -Cygpath $cygpath
$builderArguments = @()
if ($ListTargets) { $builderArguments += '--list-targets' }
if ($DryRun) { $builderArguments += '--dry-run' }

$previousEnvironment = @{}
$environmentValues = @{
    ARCH = $Arch
    PACKAGE_NAME = $PackageName
    WINDOWS_TARGETS = $WindowsTargets
    WINDOWS_SIGNTOOL = $SignTool
}
$pathEntries = @($bashDirectory)
$msysRoot = Split-Path -Parent (Split-Path -Parent $bashDirectory)
foreach ($directory in @(
    (Join-Path $msysRoot 'ucrt64\bin'),
    (Join-Path $msysRoot 'mingw64\bin')
)) {
    if (Test-Path -LiteralPath $directory -PathType Container) {
        $pathEntries += $directory
    }
}
$pathEntries += [Environment]::GetEnvironmentVariable('PATH', 'Process') -split [IO.Path]::PathSeparator
$environmentValues.PATH = ($pathEntries |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Select-Object -Unique) -join [IO.Path]::PathSeparator
if (-not [string]::IsNullOrWhiteSpace($Version)) { $environmentValues.VERSION = $Version }
if (-not [string]::IsNullOrWhiteSpace($OutDir)) {
    $environmentValues.OUT_DIR = Convert-ToPosixPath -Path $OutDir -Cygpath $cygpath
}
if (-not [string]::IsNullOrWhiteSpace($VitteExe)) {
    $compilerPath = Convert-ToPosixPath -Path $VitteExe -Cygpath $cygpath
    $environmentValues.VITTE_BIN = $compilerPath
    $environmentValues.WINDOWS_VITTE_EXE = $compilerPath
}
if ($SignTool -match '^[A-Za-z]:[\\/]' -or (Test-Path -LiteralPath $SignTool -PathType Leaf)) {
    $environmentValues.WINDOWS_SIGNTOOL = Convert-ToPosixPath -Path $SignTool -Cygpath $cygpath
}
if ($Sign) {
    if ([string]::IsNullOrWhiteSpace($SignCert)) {
        Stop-WithError '-Sign requires -SignCert'
    }
    $environmentValues.SIGN = '1'
    $environmentValues.WINDOWS_SIGN_CERT = Convert-ToPosixPath -Path $SignCert -Cygpath $cygpath
}
if ($StrictNative) { $environmentValues.STRICT_NATIVE = '1' }

$exitCode = 0
try {
    foreach ($key in $environmentValues.Keys) {
        $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, [string]$environmentValues[$key], 'Process')
    }

    $invokeArguments = @('-c', 'exec sh "$@"', 'vitte-windows-installer', $posixBuilder) + $builderArguments
    & $bash @invokeArguments
    $exitCode = $LASTEXITCODE
}
finally {
    foreach ($key in $previousEnvironment.Keys) {
        $previousValue = $previousEnvironment[$key]
        if ($null -eq $previousValue) {
            Remove-Item -Path "Env:$key" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($key, $previousValue, 'Process')
        }
    }
}

if ($exitCode -ne 0) {
    exit $exitCode
}
