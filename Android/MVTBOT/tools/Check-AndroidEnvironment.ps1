$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Activate-Android.ps1')
$checks = @(
    @{ Name='JDK runtime'; Command='java'; Arguments=@('-version') },
    @{ Name='Java compiler'; Command='javac'; Arguments=@('-version') },
    @{ Name='Gradle'; Command='gradle'; Arguments=@('--version') },
    @{ Name='ADB'; Command='adb'; Arguments=@('version') },
    @{ Name='SDK Manager'; Command='sdkmanager'; Arguments=@('--version') },
    @{ Name='Android CLI'; Command='android'; Arguments=@('--version') },
    @{ Name='JADX'; Command='jadx'; Arguments=@('--version') },
    @{ Name='Apktool'; Command='apktool'; Arguments=@('--version') },
    @{ Name='APK Signer'; Command='apksigner'; Arguments=@('version') },
    @{ Name='AAPT2'; Command='aapt2'; Arguments=@('version') }
)
$results = @()
foreach ($check in $checks) {
    $text = @(& $check.Command @($check.Arguments) 2>&1)
    $exitCode = $LASTEXITCODE
    $results += [pscustomobject]@{ Check=$check.Name; Passed=($exitCode -eq 0); ExitCode=$exitCode; Output=($text -join "`n") }
}
$results | Select-Object Check,Passed,ExitCode | Format-Table -AutoSize
Write-Output 'Connected devices (read-only; an empty list means no authorized phone is currently connected):'
adb devices -l
$failed = @($results | Where-Object { -not $_.Passed })
if ($failed.Count) { $failed | Format-List; throw "$($failed.Count) tool checks failed." }
Write-Output 'PASS: all Android tool version checks succeeded.'
