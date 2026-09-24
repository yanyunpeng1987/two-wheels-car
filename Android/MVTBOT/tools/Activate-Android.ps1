$ErrorActionPreference = 'Stop'
$bootstrap = Join-Path $env:LOCALAPPDATA 'Programs\AndroidTools\Enter-AndroidDev.ps1'
if (-not (Test-Path -LiteralPath $bootstrap)) { throw "Android environment bootstrap is missing: $bootstrap" }
. $bootstrap
