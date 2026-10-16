"""Add compiled application payloads to the SDK-generated APK in the build directory."""
from pathlib import Path
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
build = root / "build"
if sys.argv[1:] == ["preloader"]:
    with zipfile.ZipFile(root / "dist/hybridmcpe-bootstrap.levipack", "w", zipfile.ZIP_DEFLATED) as out:
        out.write(root / "bootstrap/preloader/manifest.json", "manifest.json")
        out.write(root / "bootstrap/preloader/compatibility.json", "compatibility.json")
        out.write(build / "native-arm64/libhybridmcpe.so", "libhybridmcpe.so")
    print("Built adapter package; evaluated Levi host is incompatible (see compatibility.json)")
    sys.exit(0)
if sys.argv[1:] == ["classes"]:
    with zipfile.ZipFile(build / "launcher-classes.jar", "w", zipfile.ZIP_DEFLATED) as out:
        for path in sorted((build / "java").rglob("*.class")):
            out.write(path, path.relative_to(build / "java").as_posix())
    sys.exit(0)
apk = build / "launcher-unsigned.apk"
with zipfile.ZipFile(apk, "a", compression=zipfile.ZIP_DEFLATED) as out:
    out.write(build / "dex/classes.dex", "classes.dex")
    for name in ("libhybridmcpe.so", "libiron_chest.so"):
        out.write(build / "native-arm64" / name, "lib/arm64-v8a/" + name)
print(apk)
