# Build through the same uVision project used by the Keil IDE.
# Compatible with Windows PowerShell 5.1. This file intentionally uses ASCII.
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Build', 'Rebuild', 'Check', 'Open')]
    [string]$Action = 'Build',
    [string]$KeilRoot = $env:KEIL_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Get-KeilWorkingDirectory([string]$Path) {
    if ($Path -notmatch '[^\x20-\x7E]') { return $Path }
    if (-not ('BalanceCar.NativePath' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace BalanceCar {
    public static class NativePath {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern uint GetShortPathName(string path, StringBuilder shortPath, uint capacity);
    }
}
'@
    }
    $buffer = New-Object Text.StringBuilder 32768
    $length = [BalanceCar.NativePath]::GetShortPathName($Path, $buffer, [uint32]$buffer.Capacity)
    if ($length -eq 0 -or $length -ge $buffer.Capacity -or $buffer.ToString() -match '[^\x20-\x7E]') {
        throw 'Keil cannot reliably build this Unicode path and no ASCII 8.3 alias is available. Move this workspace to an ASCII-only path, then retry. No files were moved.'
    }
    return $buffer.ToString()
}

try {
    if ([string]::IsNullOrWhiteSpace($KeilRoot)) { $KeilRoot = 'C:\Keil_v5' }
    $KeilRoot = [IO.Path]::GetFullPath($KeilRoot)
    $workspace = Split-Path -Parent $PSScriptRoot
    $projectDirectory = Join-Path $workspace 'Firmware\MDK-ARM'
    $projectFile = Join-Path $projectDirectory 'BalanceCar.uvprojx'
    $uv4 = Join-Path $KeilRoot 'UV4\UV4.exe'
    $compiler = Join-Path $KeilRoot 'ARM\ARMCLANG\bin\armclang.exe'
    Require-File $uv4
    Require-File $compiler
    Require-File $projectFile

    [xml]$project = [IO.File]::ReadAllText($projectFile)
    $targets = @($project.Project.Targets.Target | Where-Object { $_.TargetName -eq 'BalanceCar' })
    if ($targets.Count -ne 1) { throw 'Expected exactly one target named BalanceCar.' }
    $target = $targets[0]
    if ($target.uAC6 -ne '1') { throw 'BalanceCar must use Arm Compiler 6.' }
    $compilerVersion = (& $compiler --version 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot query armclang version.' }
    $versionMatch = [regex]::Match($compilerVersion, 'ARM Compiler\s+(\d+\.\d+)')
    if (-not $versionMatch.Success) { throw 'Cannot identify Arm Compiler version.' }
    $requestedVersion = [regex]::Match([string]$target.pArmCC, '::V(\d+\.\d+)::')
    if (-not $requestedVersion.Success -or $requestedVersion.Groups[1].Value -ne $versionMatch.Groups[1].Value) {
        throw "Compiler version does not match the version selected in BalanceCar.uvprojx ($($target.pArmCC))."
    }

    $common = $target.TargetOption.TargetCommonOption
    $packId = [string]$common.PackID
    if ($packId -notmatch '^([^.]+)\.([^.]+)\.(\d+\.\d+\.\d+)$') {
        throw "Unsupported device pack ID: $packId"
    }
    $packRelative = Join-Path (Join-Path $Matches[1] $Matches[2]) $Matches[3]
    $packRoots = @((Join-Path $KeilRoot 'ARM\PACK'))
    if ($env:LOCALAPPDATA) { $packRoots += (Join-Path $env:LOCALAPPDATA 'Arm\Packs') }
    if ($env:CMSIS_PACK_ROOT) { $packRoots += $env:CMSIS_PACK_ROOT }
    $installedPack = @($packRoots | Where-Object { Test-Path -LiteralPath (Join-Path $_ $packRelative) -PathType Container })
    if ($installedPack.Count -eq 0) {
        throw "Device pack $packId is missing. Install it using Keil Pack Installer."
    }
    foreach ($source in @($target.Groups.Group.Files.File)) {
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $projectDirectory ([string]$source.FilePath)))
        Require-File $sourcePath
    }
    $includePaths = ([string]$target.TargetOption.TargetArmAds.Cads.VariousControls.IncludePath).Split(';')
    foreach ($includePath in $includePaths) {
        if ([string]::IsNullOrWhiteSpace($includePath)) { continue }
        $includeDirectory = [IO.Path]::GetFullPath((Join-Path $projectDirectory $includePath.Trim().Trim('"')))
        if (-not (Test-Path -LiteralPath $includeDirectory -PathType Container)) {
            throw "Include directory not found: $includeDirectory"
        }
    }
    # Older UV4 versions cannot open project paths containing some Unicode text.
    # Use an existing Windows 8.3 alias; do not rename files or change system policy.
    $keilWorkingDirectory = Get-KeilWorkingDirectory $projectDirectory
    Write-Host "Project: $projectFile"
    Write-Host "Compiler: Arm Compiler $($versionMatch.Groups[1].Value)"
    Write-Host "Device pack: $packId"
    if ($Action -eq 'Check') {
        Write-Host 'Environment check passed. No build or device operation was performed.'
        exit 0
    }
    if ($Action -eq 'Open') {
        Start-Process -FilePath $uv4 -ArgumentList '"BalanceCar.uvprojx"' -WorkingDirectory $keilWorkingDirectory -WindowStyle Normal
        Write-Host 'Opened the BalanceCar project in Keil.'
        exit 0
    }

    $outputDirectory = [IO.Path]::GetFullPath((Join-Path $projectDirectory ([string]$common.OutputDirectory)))
    $expectedOutput = [IO.Path]::GetFullPath((Join-Path $workspace 'Firmware\build\keil'))
    if ($outputDirectory.TrimEnd('\') -ne $expectedOutput.TrimEnd('\')) {
        throw 'OutputDirectory must resolve to Firmware\build\keil.'
    }
    if ($common.OutputName -ne 'BalanceCar' -or $common.CreateExecutable -ne '1' -or $common.CreateHexFile -ne '1') {
        throw 'Expected executable and HEX outputs named BalanceCar.'
    }
    [void][IO.Directory]::CreateDirectory($outputDirectory)
    # A unique log per invocation makes stale build logs impossible to accept.
    $logName = '{0}-{1}-{2}.log' -f $Action.ToLowerInvariant(), (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
    $logPath = Join-Path $outputDirectory $logName
    $command = if ($Action -eq 'Rebuild') { '-r' } else { '-b' }
    # Relative command-line paths avoid passing the Unicode workspace name to UV4.
    $arguments = @($command, '"BalanceCar.uvprojx"', '-j0', '-t', '"BalanceCar"', '-o', ('"..\build\keil\{0}"' -f $logName))
    $startedUtc = [DateTime]::UtcNow
    $process = Start-Process -FilePath $uv4 -ArgumentList $arguments -WorkingDirectory $keilWorkingDirectory -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    Require-File $logPath
    $log = [IO.File]::ReadAllText($logPath, [Text.Encoding]::Default)
    Write-Host $log
    Write-Host "Build log: $logPath"
    if ($exitCode -notin @(0, 1)) { throw "uVision failed with exit code $exitCode." }
    $summaries = [regex]::Matches($log, '(?im)^.*?\b(\d+)\s+Error\(s\),\s*(\d+)\s+Warning\(s\)\.')
    $upToDate = $log -match '(?i)up[- ]to[- ]date'
    if ($summaries.Count -eq 0 -and -not ($Action -eq 'Build' -and $upToDate)) {
        throw 'No successful build summary was found in the current build log.'
    }
    foreach ($summary in $summaries) {
        if ([int]$summary.Groups[1].Value -ne 0) { throw 'Build log reports compilation errors.' }
    }
    foreach ($extension in @('axf', 'hex')) {
        $artifact = Join-Path $outputDirectory "BalanceCar.$extension"
        Require-File $artifact
        $artifactInfo = Get-Item -LiteralPath $artifact
        if ($artifactInfo.Length -eq 0) { throw "Empty build artifact: $artifact" }
        if ($Action -eq 'Rebuild' -and $artifactInfo.LastWriteTimeUtc -lt $startedUtc.AddSeconds(-2)) {
            throw "Rebuild did not refresh artifact: $artifact"
        }
    }
    if ($exitCode -eq 1) { Write-Warning 'Build succeeded with warnings; inspect the build log.' }
    Write-Host "$Action succeeded. Outputs: $outputDirectory"
    # UV4 code 1 means warnings only; report success to VS Code tasks.
    exit 0
}
catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
