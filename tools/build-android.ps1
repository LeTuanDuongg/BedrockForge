param([Parameter(Mandatory=$true)][string]$Ndk,[string]$Preloader,[string]$Abi='arm64-v8a')
$ErrorActionPreference='Stop'
if($Abi -ne 'arm64-v8a'){throw 'Initial launcher profile is ARM64 only'}
$compilerToolchain=Join-Path $Ndk 'build/cmake/android.toolchain.cmake'
if(!(Test-Path -LiteralPath $compilerToolchain)){throw 'NDK toolchain missing'}
$ndkMetadata=Get-Content -LiteralPath (Join-Path $Ndk 'source.properties') -Raw
if($ndkMetadata -notmatch 'Pkg.Revision\s*=\s*27\.2\.12479018'){throw 'Use pinned NDK 27.2.12479018 (untested Android baseline)'}
$argsList=@('-S','.', '-B','build-android','-G','Ninja',"-DCMAKE_TOOLCHAIN_FILE=$compilerToolchain", "-DANDROID_ABI=$Abi",'-DANDROID_PLATFORM=android-28','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF')
if($Preloader){$argsList+=@('-DBF_LEVI=ON',"-DPRELOADER_ANDROID_ROOT=$Preloader")}
& cmake @argsList
if($LASTEXITCODE){throw 'Android configure failed'}
& cmake --build build-android
if($LASTEXITCODE){throw 'Android build failed'}
