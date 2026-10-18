"""Build signed import-only APK fixtures with the local target's real ELF. Never installs or executes them."""
import argparse
import shutil
import subprocess
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--jdk", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    work = root / "build/apk-import-fixtures"
    work.mkdir(parents=True, exist_ok=True)
    tools = args.sdk / "build-tools/36.0.0"
    java = args.jdk / "bin/java.exe"
    android = args.sdk / "platforms/android-36/android.jar"

    def run(*command):
        subprocess.run([str(c) for c in command], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    keys = [root / "build/debug.keystore", work / "other.keystore"]
    if not keys[1].exists():
        run(args.jdk / "bin/keytool.exe", "-genkeypair", "-keystore", keys[1], "-storepass", "android",
            "-keypass", "android", "-alias", "fixture", "-dname", "CN=HybridMCPE Import Test", "-keyalg", "RSA", "-validity", "365")

    def fixture(name, split, version, key, engine=False):
        manifest = work / (name + ".xml")
        manifest.write_text(
            '<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
            f'package="dev.hybridmcpe.importfixture" android:versionCode="{version}" android:versionName="split-test" '
            + (f'split="{split}"' if split else "") + '><uses-sdk android:minSdkVersion="31" android:targetSdkVersion="36"/>'
            + '<application android:isSplitRequired="true"/></manifest>', encoding="utf-8")
        unsigned = work / (name + "-unsigned.apk")
        run(tools / "aapt2.exe", "link", "-I", android, "--manifest", manifest, "-o", unsigned)
        if engine:
            with zipfile.ZipFile(args.apk) as source, zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as out:
                with source.open("lib/arm64-v8a/libminecraftpe.so") as src, out.open("lib/arm64-v8a/libminecraftpe.so", "w") as dst:
                    shutil.copyfileobj(src, dst)
        result = work / (name + ".apk")
        run(java, "-jar", tools / "lib/apksigner.jar", "sign", "--ks", keys[key], "--ks-pass", "pass:android",
            "--key-pass", "pass:android", "--out", result, unsigned)
        return result

    base = fixture("base", "", 7, 0)
    split = fixture("arm64", "config.arm64_v8a", 7, 0, engine=True)
    wrong_signer = fixture("wrong-signer", "config.arm64_v8a", 7, 1)
    wrong_version = fixture("wrong-version", "config.arm64_v8a", 8, 0)
    run(args.jdk / "bin/javac.exe", "--release", "17", "-cp", root / "build/apksig.jar", "-d", root / "build/import-tests",
        root / "android/src/dev/hybridmcpe/launcher/ApkRepository.java", root / "tests/java/ApkRepositoryTests.java")
    cp = str(root / "build/import-tests") + ";" + str(root / "build/apksig.jar")
    result = subprocess.run([str(java), "-cp", cp, "dev.hybridmcpe.launcher.ApkRepositoryTests", str(args.apk),
                             str(base), str(split), str(wrong_signer), str(wrong_version)], check=True, capture_output=True, text=True)
    print(result.stdout.strip())


if __name__ == "__main__":
    main()
