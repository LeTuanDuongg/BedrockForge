param(
    [string]$Apk = 'C:/MCPE_Research/base.apk',
    [string]$Sdk = 'C:/Users/letua/AppData/Local/Android/Sdk',
    [string]$Jdk = 'C:/Users/letua/Documents/ChatGPT/ReverseEngine/tools/jdk/jdk-21.0.12.1+1',
    [string]$Python = 'C:/Users/letua/AppData/Local/Programs/Python/Python314/python.exe',
    [switch]$Control,
    [switch]$PayloadOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$prepared = "$root/build/modified"
$native = "$root/build/modified-native"
$tools = "$Sdk/build-tools/36.0.0"
function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}
New-Item -ItemType Directory -Force -Path $prepared, "$root/build/tmp", "$root/dist" | Out-Null
$env:JAVA_HOME = $Jdk
$env:JDK_JAVA_OPTIONS = "-Djava.io.tmpdir=$root/build/tmp"
Run $Python @('-B', "$root/tools/patch_modified_game.py", 'prepare', '--apk', $Apk, '--out-dir', $prepared)
if (-not $Control) {
Run "$Sdk/cmake/3.22.1/bin/cmake.exe" @('-S', $root, '-B', $native, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$Sdk/cmake/3.22.1/bin/ninja.exe", "-DCMAKE_TOOLCHAIN_FILE=$Sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake", '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-31', '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DHYBRID_BOOTSTRAP_PRELOADER=OFF', "-DHYBRID_MODIFIED_PROFILE_DIR=$prepared")
Run "$Sdk/cmake/3.22.1/bin/cmake.exe" @('--build', $native)
if (-not $Control) {
    $packageArgs = @('-B', "$root/tools/patch_modified_game.py", 'package', '--apk', $Apk, '--prepared', $prepared, '--runtime', "$native/libhybridmcpe.so", '--lifecycle-mod', "$native/libhybrid_bootstrap_check.so", '--output', "$prepared/unsigned.apk")
    if ($PayloadOnly) { $packageArgs += '--payload-only' }
    Run $Python $packageArgs
}
}
$stem = if ($Control) { 'clone-control' } elseif ($PayloadOnly) { 'payload-only' } else { 'local-game' }
$unsigned = if ($Control) { "$prepared/control-unsigned.apk" } else { "$prepared/unsigned.apk" }
if ($Control) { Run $Python @('-B', "$root/tools/patch_modified_game.py", 'control', '--apk', $Apk, '--prepared', $prepared, '--output', $unsigned) }
Run "$tools/zipalign.exe" @('-f', '-P', '16', '4', $unsigned, "$prepared/$stem-aligned.apk")
$key = "$root/build/modified/research.keystore"
if (-not (Test-Path -LiteralPath $key)) {
    Run "$Jdk/bin/keytool.exe" @('-genkeypair', '-keystore', $key, '-storepass', 'android', '-keypass', 'android', '-alias', 'hybrid-research', '-dname', 'CN=HybridMCPE Local Research', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650')
}
$signed = "$root/dist/HybridMCPE-$stem.apk"
Run "$tools/apksigner.bat" @('sign', '--ks', $key, '--ks-key-alias', 'hybrid-research', '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', $signed, "$prepared/$stem-aligned.apk")
Run "$tools/apksigner.bat" @('verify', '--verbose', $signed)
Run "$tools/zipalign.exe" @('-c', '-P', '16', '4', $signed)
$verifyArgs = @('-B', "$root/tools/verify_modified_game.py", '--source', $Apk, '--modified', $signed, '--prepared', $prepared)
if ($Control) { $verifyArgs += '--control' }
if ($PayloadOnly) { $verifyArgs += '--payload-only' }
Run $Python $verifyArgs
Write-Output "Local proprietary APK built; do not distribute. Package: dev.hybridmcpe.research; payloadOnly=$PayloadOnly"
