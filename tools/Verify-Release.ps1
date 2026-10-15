# Read-only verification. Does not connect to hardware, build, erase or program.
[CmdletBinding()]
param([string]$ReleaseDirectory)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $current = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CURRENT.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $ReleaseDirectory = Join-Path $PSScriptRoot $current.package
}
$packageRoot = [IO.Path]::GetFullPath($ReleaseDirectory).TrimEnd('\','/')
$checks = Get-Content -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.txt') -Encoding ASCII
foreach ($line in $checks) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum entry' }
    $expected = $Matches[1]
    $relative = $Matches[2]
    $target = [IO.Path]::GetFullPath((Join-Path $packageRoot $relative))
    if (-not $target.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path outside package' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw ('Hash mismatch: ' + $relative) }
}
$manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$hex = Join-Path $packageRoot $manifest.entryHex
if ((Get-FileHash -LiteralPath $hex -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.hexSha256) { throw 'HEX identity mismatch' }
Write-Output ('PASS: ' + $manifest.packageId)
Write-Output ('Firmware HEX: ' + $hex)
Write-Output 'No hardware operation performed. Preserve PID Sector1 when programming.'
