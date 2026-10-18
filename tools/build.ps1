param(
    [string]$Sdk = 'C:/Users/letua/AppData/Local/Android/Sdk',
    [string]$Jdk = 'C:/Users/letua/Documents/ChatGPT/ReverseEngine/tools/jdk/jdk-21.0.12.1+1',
    [string]$Python = 'C:/Users/letua/AppData/Local/Programs/Python/Python314/python.exe',
    [switch]$PreloaderAdapter,
    [switch]$ExperimentalNative,
    [switch]$ExperimentalEngineHook,
    [switch]$ExperimentalDebugger
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$tools = Join-Path $Sdk 'build-tools/36.0.0'
$android = Join-Path $Sdk 'platforms/android-36/android.jar'
$cmake = Join-Path $Sdk 'cmake/4.1.2/bin/cmake.exe'
$ninja = Join-Path $Sdk 'cmake/4.1.2/bin/ninja.exe'
$ndk = Join-Path $Sdk 'ndk/28.2.13676358/build/cmake/android.toolchain.cmake'
function Run([string]$program, [string[]]$arguments) {
    # Native tools emit successful compiler notes/progress on stderr too.
    $ErrorActionPreference = 'Continue'
    & $program @arguments 2>&1 | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message }
        else { $_.ToString() }
    }
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}
New-Item -ItemType Directory -Force -Path "$build/java", "$build/game-api", "$build/dex", "$build/tmp", "$root/dist" | Out-Null
$env:JAVA_HOME = $Jdk
$env:JDK_JAVA_OPTIONS = "-Djava.io.tmpdir=$build/tmp"
$adapter = if ($PreloaderAdapter) { 'ON' } else { 'OFF' }
$experimental = if ($ExperimentalNative) { 'ON' } else { 'OFF' }
$engineHook = if ($ExperimentalEngineHook) { 'ON' } else { 'OFF' }
$env:TEMP = "$build/tmp"
$env:TMP = "$build/tmp"
Run $cmake @('-S', $root, '-B', "$build/native-arm64", '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_TOOLCHAIN_FILE=$ndk", '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-31', '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DHYBRID_BOOTSTRAP_PRELOADER=$adapter", "-DHYBRID_EXPERIMENTAL_NATIVE=$experimental", "-DHYBRID_EXPERIMENTAL_ENGINE_HOOK=$engineHook")
Run $cmake @('--build', "$build/native-arm64")
Run $Python @("$root/tools/package_apk.py", 'dependencies', $tools)
Run "$Jdk/bin/javac.exe" @('--release', '17', '-classpath', $android, '-d', "$build/game-api", "$root/android/compileOnly/com/mojang/minecraftpe/MainActivity.java")
$sources = @(Get-ChildItem -LiteralPath "$root/android/src" -Filter '*.java' -Recurse | ForEach-Object { $_.FullName })
Run "$Jdk/bin/javac.exe" (@('--release', '17', '-classpath', "$android;$build/apksig.jar;$build/game-api", '-d', "$build/java") + $sources)
Run $Python @("$root/tools/package_apk.py", 'classes')
Run "$tools/d8.bat" @('--min-api', '31', '--lib', $android, '--classpath', "$build/game-api", '--output', "$build/dex", "$build/launcher-classes.jar", "$build/apksig.jar")
$manifestArgs = @('link', '-I', $android, '--manifest', "$root/android/AndroidManifest.xml")
if ($ExperimentalDebugger) { $manifestArgs += '--debug-mode' }
$manifestArgs += @('-o', "$build/launcher-unsigned.apk")
Run "$tools/aapt2.exe" $manifestArgs
Run $Python @("$root/tools/package_apk.py")
Run "$tools/zipalign.exe" @('-f', '-P', '16', '4', "$build/launcher-unsigned.apk", "$build/launcher-aligned.apk")
$key = "$build/debug.keystore"
if (-not (Test-Path -LiteralPath $key)) {
    Run "$Jdk/bin/keytool.exe" @('-genkeypair', '-keystore', $key, '-storepass', 'android', '-keypass', 'android', '-alias', 'hybrid-debug', '-dname', 'CN=HybridMCPE Development', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650')
}
Run "$tools/apksigner.bat" @('sign', '--ks', $key, '--ks-key-alias', 'hybrid-debug', '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', "$root/dist/HybridMCPE-debug.apk", "$build/launcher-aligned.apk")
Run "$tools/apksigner.bat" @('verify', '--verbose', "$root/dist/HybridMCPE-debug.apk")
Write-Output "Built $root/dist/HybridMCPE-debug.apk"
if ($PreloaderAdapter) { Run $Python @("$root/tools/package_apk.py", 'preloader') }
