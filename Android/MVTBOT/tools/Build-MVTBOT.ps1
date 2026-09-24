#requires -Version 5.1
<#
.SYNOPSIS
Builds MVTBOT from a private project snapshot without installing it.
.DESCRIPTION
Requires the existing development certificate for signed builds. This script
never creates signing keys, persists environment changes, or accesses devices.
Use -Unsigned when the original signing material is unavailable.
#>
[CmdletBinding()]
param(
    [string]$ModuleRoot,
    [string]$OutputDirectory,
    [string]$ApktoolJar,
    [string]$JavaHome,
    [string]$AndroidSdk,
    [string]$BuildToolsVersion = '36.0.0',
    [string]$KeyStorePath,
    [string]$PasswordFile,
    [switch]$Unsigned
)

$ErrorActionPreference = 'Stop'
$expectedSigner = '71e85f94fb18c04425c908661e7321dce8dc182e7077cc541004aaf539e15287'
$keyAlias = 'mvtbot-dev'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$secretEnvironment = @{}
$securePassword = $null
$passwordPointer = [IntPtr]::Zero
$stage = 'Preflight'
$reportPath = $null

function Resolve-FileSystemPath([string]$Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, $script:utf8)
}

function Quote-NativeArgument([AllowEmptyString()][string]$Value) {
    # Windows CommandLineToArgvW/CRT quoting; no command shell is involved.
    $builder = New-Object Text.StringBuilder
    [void]$builder.Append([char]34)
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { $slashes++; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $slashes + 1))
            [void]$builder.Append([char]34)
        } else {
            if ($slashes -gt 0) { [void]$builder.Append([char]92, $slashes) }
            [void]$builder.Append($character)
        }
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append([char]92, (2 * $slashes)) }
    [void]$builder.Append([char]34)
    return $builder.ToString()
}

function Invoke-BuildTool {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$LogPath,
        [hashtable]$ExtraEnvironment = @{}
    )
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($Arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' ')
    $info.WorkingDirectory = $script:OutputDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $script:utf8
    $info.StandardErrorEncoding = $script:utf8
    foreach ($name in $ExtraEnvironment.Keys) { $info.EnvironmentVariables[$name] = $ExtraEnvironment[$name] }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw "Unable to start tool: $Executable" }
        # The child now owns its environment snapshot. Remove our extra values.
        foreach ($name in $ExtraEnvironment.Keys) { $info.EnvironmentVariables.Remove($name) }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
        $text = $stdout + [Environment]::NewLine + $stderr
        Write-Utf8 $LogPath $text
        if ($exitCode -ne 0) { throw "Tool exited with code $exitCode. See $LogPath" }
        return [pscustomobject]@{ ExitCode = $exitCode; Text = $text.Trim(); Log = $LogPath }
    } finally {
        foreach ($name in $ExtraEnvironment.Keys) { $info.EnvironmentVariables.Remove($name) }
        $process.Dispose()
    }
}

function Get-StreamSha256([IO.Stream]$Stream) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-ZipCompressionMethods([string]$Path) {
    # Read central-directory metadata so stored native libraries are verified
    # without relying on compressed length or undocumented .NET properties.
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        $tailLength = [int][Math]::Min(65557, $stream.Length)
        [void]$stream.Seek(-$tailLength, [IO.SeekOrigin]::End)
        $tail = $reader.ReadBytes($tailLength)
        $end = -1
        for ($i = $tail.Length - 22; $i -ge 0; $i--) {
            if ([BitConverter]::ToUInt32($tail, $i) -eq 0x06054b50 -and
                $i + 22 + [BitConverter]::ToUInt16($tail, $i + 20) -eq $tail.Length) {
                $end = $i
                break
            }
        }
        if ($end -lt 0) { throw 'APK ZIP end record was not found.' }
        $count = [BitConverter]::ToUInt16($tail, $end + 10)
        $offset = [BitConverter]::ToUInt32($tail, $end + 16)
        if ($count -eq 65535 -or $offset -eq [uint32]::MaxValue) { throw 'ZIP64 APKs are not supported by this verification script.' }
        [void]$stream.Seek($offset, [IO.SeekOrigin]::Begin)
        $methods = @{}
        for ($i = 0; $i -lt $count; $i++) {
            $header = $reader.ReadBytes(46)
            if ($header.Length -ne 46 -or [BitConverter]::ToUInt32($header, 0) -ne 0x02014b50) {
                throw 'Invalid APK ZIP central directory.'
            }
            $nameLength = [BitConverter]::ToUInt16($header, 28)
            $extraLength = [BitConverter]::ToUInt16($header, 30)
            $commentLength = [BitConverter]::ToUInt16($header, 32)
            $name = [Text.Encoding]::UTF8.GetString($reader.ReadBytes($nameLength))
            if ($methods.ContainsKey($name)) { throw "Duplicate APK entry: $name" }
            $methods[$name] = [BitConverter]::ToUInt16($header, 10)
            [void]$stream.Seek(($extraLength + $commentLength), [IO.SeekOrigin]::Current)
        }
        return $methods
    } finally { $reader.Dispose(); $stream.Dispose() }
}

$report = [ordered]@{
    status = 'STARTED'
    startedAt = (Get-Date).ToString('o')
    unsigned = [bool]$Unsigned
    expectedSignerSha256 = $expectedSigner
    deviceOperations = $false
    sourceProjectModified = $false
}

try {
    if (-not $ModuleRoot) { $ModuleRoot = Split-Path -Parent $PSScriptRoot }
    $ModuleRoot = Resolve-FileSystemPath $ModuleRoot
    $sourceProject = Join-Path $ModuleRoot 'project'
    $frameworkDirectory = Join-Path $ModuleRoot 'materials\tooling\framework'
    if (-not $ApktoolJar) { $ApktoolJar = Join-Path $env:LOCALAPPDATA 'Programs\AndroidTools\apktool-3.0.3\apktool_3.0.3.jar' }
    if (-not $JavaHome) { $JavaHome = Join-Path $env:LOCALAPPDATA 'Programs\AndroidTools\jdk-21' }
    if (-not $AndroidSdk) {
        $AndroidSdk = $env:ANDROID_HOME
        if (-not $AndroidSdk) { $AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    }
    $ApktoolJar = Resolve-FileSystemPath $ApktoolJar
    $JavaHome = Resolve-FileSystemPath $JavaHome
    $AndroidSdk = Resolve-FileSystemPath $AndroidSdk
    $java = Join-Path $JavaHome 'bin\java.exe'
    $keytool = Join-Path $JavaHome 'bin\keytool.exe'
    $buildTools = Join-Path $AndroidSdk ('build-tools\' + $BuildToolsVersion)
    $zipalign = Join-Path $buildTools 'zipalign.exe'
    $aapt2 = Join-Path $buildTools 'aapt2.exe'
    $apksignerJar = Join-Path $buildTools 'lib\apksigner.jar'
    foreach ($item in @(
        @((Join-Path $sourceProject 'apktool.yml'), 'Apktool project'),
        @((Join-Path $sourceProject 'AndroidManifest.xml'), 'Project manifest'),
        @((Join-Path $frameworkDirectory '1.apk'), 'Pinned Android framework'),
        @($ApktoolJar, 'Apktool JAR'), @($java, 'JDK Java'),
        @($zipalign, 'zipalign'), @($aapt2, 'aapt2')
    )) { Assert-File $item[0] $item[1] }
    if (-not $Unsigned) {
        if (-not $KeyStorePath) { $KeyStorePath = Join-Path $env:LOCALAPPDATA 'Android\Signing\MVTBOT\mvtbot-development.p12' }
        if (-not $PasswordFile) { $PasswordFile = Join-Path $env:LOCALAPPDATA 'Android\Signing\MVTBOT\keystore-password.dpapi' }
        $KeyStorePath = Resolve-FileSystemPath $KeyStorePath
        $PasswordFile = Resolve-FileSystemPath $PasswordFile
        Assert-File $keytool 'JDK keytool'
        Assert-File $apksignerJar 'apksigner JAR'
        Assert-File $KeyStorePath 'Existing MVTBOT development keystore; use -Unsigned if unavailable'
        Assert-File $PasswordFile 'Existing DPAPI password file; use -Unsigned if unavailable'
    }
    if (-not $OutputDirectory) {
        $runName = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
        $OutputDirectory = Join-Path $ModuleRoot ('build\' + $runName)
    }
    $OutputDirectory = Resolve-FileSystemPath $OutputDirectory
    if (Test-Path -LiteralPath $OutputDirectory) { throw "Refusing to overwrite an existing output directory: $OutputDirectory" }
    $sourcePrefix = $sourceProject.TrimEnd([char]92, [char]47) + [IO.Path]::DirectorySeparatorChar
    if ($OutputDirectory -eq $sourceProject -or $OutputDirectory.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The output directory must be outside the canonical project directory.'
    }
    [void][IO.Directory]::CreateDirectory($OutputDirectory)
    $reportPath = Join-Path $OutputDirectory 'build-summary.json'
    $report.moduleRoot = $ModuleRoot
    $report.sourceProject = $sourceProject
    $report.outputDirectory = $OutputDirectory
    $report.tools = [ordered]@{ javaHome = $JavaHome; apktoolJar = $ApktoolJar; apktoolSha256 = (Get-FileHash -LiteralPath $ApktoolJar -Algorithm SHA256).Hash.ToLowerInvariant(); androidSdk = $AndroidSdk; buildToolsVersion = $BuildToolsVersion }
    Write-Host "Build output: $OutputDirectory"

    $stage = 'JDK verification'
    $javaResult = Invoke-BuildTool $java @('-version') (Join-Path $OutputDirectory 'java-version.log')
    if ($javaResult.Text -notmatch '(?:openjdk|java) version "21(?:\.|\")') { throw 'This build requires JDK 21. Point -JavaHome to the installed JDK 21, not Studio JBR 25.' }
    $report.javaVersion = $javaResult.Text

    if (-not $Unsigned) {
        $stage = 'Existing signer verification'
        try { $securePassword = (Get-Content -LiteralPath $PasswordFile -Raw).Trim() | ConvertTo-SecureString }
        catch { throw 'Cannot decrypt the DPAPI password with the current Windows account. Use -Unsigned or restore authorized signing access.' }
        $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
        $secretName = 'MVTBOT_BUILD_PASSWORD_' + [Guid]::NewGuid().ToString('N')
        try { $secretEnvironment[$secretName] = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer) }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer); $passwordPointer = [IntPtr]::Zero }
        $certificatePath = Join-Path $OutputDirectory 'signer-certificate.der'
        $null = Invoke-BuildTool $keytool @('-exportcert', '-keystore', $KeyStorePath, '-storetype', 'PKCS12', '-alias', $keyAlias, '-storepass:env', $secretName, '-file', $certificatePath) (Join-Path $OutputDirectory 'signer-preflight.log') $secretEnvironment
        $certificateHash = (Get-FileHash -LiteralPath $certificatePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($certificateHash -ne $expectedSigner) { throw 'The existing keystore does not contain the expected MVTBOT development certificate. No replacement key will be generated.' }
        $report.signerSha256 = $certificateHash
    }

    $stage = 'Project snapshot'
    $snapshot = Join-Path $OutputDirectory 'project'
    [void][IO.Directory]::CreateDirectory($snapshot)
    foreach ($child in Get-ChildItem -LiteralPath $sourceProject -Force) {
        # Exclude only the project root build cache. Nested build folders matter.
        if ($child.PSIsContainer -and $child.Name -eq 'build') { continue }
        Copy-Item -LiteralPath $child.FullName -Destination $snapshot -Recurse -Force
    }
    $snapshotFramework = Join-Path $OutputDirectory 'framework'
    Copy-Item -LiteralPath $frameworkDirectory -Destination $snapshotFramework -Recurse
    $report.snapshotProject = $snapshot

    $stage = 'Apktool build'
    Write-Host 'Compiling the project snapshot with Apktool (2 workers).'
    $unsignedApk = Join-Path $OutputDirectory 'MVTBOT-unsigned.apk'
    $javaOptions = @('-Xmx2g', '-Dfile.encoding=UTF-8', '-Duser.language=en', '-Duser.country=US')
    $null = Invoke-BuildTool $java ($javaOptions + @('-jar', $ApktoolJar, 'b', $snapshot, '--frame-path', $snapshotFramework, '--jobs', '2', '-o', $unsignedApk)) (Join-Path $OutputDirectory 'apktool-build.log')
    Assert-File $unsignedApk 'Built APK'

    $stage = 'ZIP alignment'
    $alignedApk = Join-Path $OutputDirectory 'MVTBOT-aligned-unsigned.apk'
    $null = Invoke-BuildTool $zipalign @('-P', '16', '4', $unsignedApk, $alignedApk) (Join-Path $OutputDirectory 'zipalign-build.log')
    $finalApk = $alignedApk
    if (-not $Unsigned) {
        $stage = 'APK signing'
        $finalApk = Join-Path $OutputDirectory 'MVTBOT.apk'
        $null = Invoke-BuildTool $java ($javaOptions + @('-jar', $apksignerJar, 'sign', '--ks', $KeyStorePath, '--ks-key-alias', $keyAlias, '--ks-pass', ('env:' + $secretName), '--key-pass', ('env:' + $secretName), '--v4-signing-enabled', 'false', '--out', $finalApk, $alignedApk)) (Join-Path $OutputDirectory 'apksigner-sign.log') $secretEnvironment
        $secretEnvironment.Clear()
        $securePassword.Dispose()
        $securePassword = $null
        $stage = 'APK signature verification'
        $signature = Invoke-BuildTool $java ($javaOptions + @('-jar', $apksignerJar, 'verify', '--verbose', '--print-certs', $finalApk)) (Join-Path $OutputDirectory 'signature-verification.log')
        $signers = [regex]::Matches($signature.Text, '(?m)^Signer #\d+ certificate SHA-256 digest:\s*([0-9a-fA-F]{64})\s*$')
        if ($signers.Count -ne 1 -or $signers[0].Groups[1].Value.ToLowerInvariant() -ne $expectedSigner) { throw 'The final APK signer does not match the expected development certificate.' }
        $report.signatureVerified = $true
    } else { $report.signatureVerified = $false }

    $stage = 'Final APK verification'
    $null = Invoke-BuildTool $zipalign @('-c', '-P', '16', '-v', '4', $finalApk) (Join-Path $OutputDirectory 'alignment-verification.log')
    $badging = Invoke-BuildTool $aapt2 @('dump', 'badging', $finalApk) (Join-Path $OutputDirectory 'apk-badging.log')
    $packageMatch = [regex]::Match($badging.Text, "(?m)^package: name='([^']+)' versionCode='([^']+)' versionName='([^']*)'")
    if (-not $packageMatch.Success) { throw 'Unable to verify the compiled APK package and version metadata.' }
    [xml]$sourceManifest = Get-Content -LiteralPath (Join-Path $snapshot 'AndroidManifest.xml') -Raw
    if ($packageMatch.Groups[1].Value -ne $sourceManifest.manifest.package) { throw 'The compiled package does not match the project manifest.' }
    $report.packageName = $packageMatch.Groups[1].Value
    $report.versionCode = $packageMatch.Groups[2].Value
    $report.versionName = $packageMatch.Groups[3].Value
    $report.manifestSummary = @($badging.Text -split '\r?\n' | Where-Object { $_ -match '^(package:|(?:min)?sdkVersion:|targetSdkVersion:|application-label|launchable-activity:|native-code:)' })

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $methods = Get-ZipCompressionMethods $finalApk
    if (-not $methods.ContainsKey('resources.arsc') -or $methods['resources.arsc'] -ne 0) { throw 'resources.arsc is missing or compressed.' }
    $payloadFiles = @()
    foreach ($file in Get-ChildItem -LiteralPath $snapshot -File -Filter '*.dex') {
        $payloadFiles += [pscustomobject]@{ Source = $file.FullName; Entry = $file.Name }
    }
    if (@($payloadFiles).Count -eq 0) { throw 'No preserved DEX payload was found in the project snapshot.' }
    foreach ($mapping in @(@('assets', 'assets'), @('lib', 'lib'), @('unknown\META-INF\services', 'META-INF/services'))) {
        $directory = Join-Path $snapshot $mapping[0]
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse) {
            $relative = $file.FullName.Substring($directory.Length).TrimStart([char]92, [char]47).Replace('\', '/')
            $payloadFiles += [pscustomobject]@{ Source = $file.FullName; Entry = ($mapping[1] + '/' + $relative) }
        }
    }
    $archive = [IO.Compression.ZipFile]::OpenRead($finalApk)
    $checks = @()
    try {
        foreach ($file in $payloadFiles) {
            $entry = $archive.GetEntry($file.Entry)
            if (-not $entry) { throw "Required APK payload is missing: $($file.Entry)" }
            $stream = $entry.Open()
            try { $actual = Get-StreamSha256 $stream } finally { $stream.Dispose() }
            $expected = (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $expected) { throw "APK payload changed: $($file.Entry)" }
            if ($file.Entry -like 'lib/*.so' -and $methods[$file.Entry] -ne 0) { throw "Native library is compressed: $($file.Entry)" }
            $checks += [pscustomobject]@{ entry = $file.Entry; sha256 = $actual; bytesUnchanged = $true }
        }
    } finally { $archive.Dispose() }
    # PNGs may be losslessly re-encoded by AAPT; image pixels are a separate gate.
    $report.payloadChecks = $checks
    $report.alignmentVerified = $true
    $report.apk = $finalApk
    $report.apkBytes = (Get-Item -LiteralPath $finalApk).Length
    $report.apkSha256 = (Get-FileHash -LiteralPath $finalApk -Algorithm SHA256).Hash.ToLowerInvariant()
    $report.status = 'PASS'
    $report.completedAt = (Get-Date).ToString('o')
    Write-Utf8 $reportPath ($report | ConvertTo-Json -Depth 8)
    Write-Utf8 (Join-Path $OutputDirectory 'SHA256SUMS.txt') ($report.apkSha256 + '  ' + [IO.Path]::GetFileName($finalApk) + [Environment]::NewLine)
    $summary = @('MVTBOT build: PASS', ('Package: ' + $report.packageName), ('Version: ' + $report.versionName + ' (' + $report.versionCode + ')'), ('Unsigned: ' + [bool]$Unsigned), ('APK: ' + $finalApk), ('SHA-256: ' + $report.apkSha256), ('Unchanged payload entries: ' + $checks.Count), 'ZIP alignment: 16 KB verification passed', 'No device installation or device operation was performed.')
    Write-Utf8 (Join-Path $OutputDirectory 'build-summary.txt') ($summary -join [Environment]::NewLine)
    Write-Host ($summary -join [Environment]::NewLine)
} catch {
    $report.status = 'FAILED'
    $report.failedStage = $stage
    $report.error = $_.Exception.Message
    $report.completedAt = (Get-Date).ToString('o')
    if ($reportPath) {
        try { Write-Utf8 $reportPath ($report | ConvertTo-Json -Depth 8) } catch { }
    }
    throw
} finally {
    $secretEnvironment.Clear()
    if ($passwordPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer) }
    if ($securePassword) { $securePassword.Dispose() }
}
