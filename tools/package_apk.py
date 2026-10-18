"""Add compiled application payloads to the SDK-generated APK in the build directory."""
from pathlib import Path
import sys
import zipfile
import hashlib

root = Path(__file__).resolve().parents[1]
build = root / "build"
if len(sys.argv) == 3 and sys.argv[1] == "dependencies":
    sdk_tools = Path(sys.argv[2])
    jar = sdk_tools / "lib/apksigner.jar"
    expected = "3716d9311e55d2b0918a2fd9d54ba9e406c5f6abeea700b287f11259bc163dec"
    if hashlib.sha256(jar.read_bytes()).hexdigest() != expected:
        raise SystemExit("Unsupported apksig build: verify API and update pin explicitly")
    with zipfile.ZipFile(jar) as source, zipfile.ZipFile(build / "apksig.jar", "w", zipfile.ZIP_DEFLATED) as out:
        for entry in source.infolist():
            if entry.filename.startswith("com/android/apksig/") and entry.filename.endswith(".class"):
                out.writestr(entry.filename, source.read(entry))
    (build / "apksig-NOTICE.txt").write_bytes((sdk_tools / "NOTICE.txt").read_bytes())
    sys.exit(0)
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
            if path.relative_to(build / "java").as_posix().startswith("com/mojang/"):
                raise SystemExit("Compile-only game API must never ship")
            out.write(path, path.relative_to(build / "java").as_posix())
    sys.exit(0)
apk = build / "launcher-unsigned.apk"
with zipfile.ZipFile(apk, "a", compression=zipfile.ZIP_DEFLATED) as out:
    out.write(build / "dex/classes.dex", "classes.dex")
    out.write(build / "apksig-NOTICE.txt", "assets/third-party/apksig-NOTICE.txt")
    cache = (build / "native-arm64/CMakeCache.txt").read_text(errors="replace")
    if "HYBRID_EXPERIMENTAL_NATIVE:BOOL=ON" in cache:
        out.write(root / "third_party/shadowhook/NOTICE.txt", "assets/third-party/shadowhook-NOTICE.txt")
        out.write(root / "third_party/shadowhook/LICENSE", "assets/third-party/shadowhook-LICENSE.txt")
        out.write(root / "third_party/shadowhook/third_party/xdl/LICENSE", "assets/third-party/shadowhook-xdl-LICENSE.txt")
        out.write(root / "third_party/shadowhook/third_party/lss/LICENSE", "assets/third-party/shadowhook-lss-LICENSE.txt")
    names = ["libhybridmcpe.so", "libiron_chest.so", "libhybrid_diagnostic.so",
             "libhybrid_sdk_events_example.so"]
    if "HYBRID_EXPERIMENTAL_NATIVE:BOOL=ON" in cache:
        names.extend(["libhybrid_experimental_registry_probe.so", "libhybrid_button_duration_mod.so", "libshadowhook.so", "libshadowhook_nothing.so"])
    for name in names:
        source = build / "native-arm64" / name
        if not source.exists():
            source = build / "native-arm64/shadowhook-build" / name
        out.write(source, "lib/arm64-v8a/" + name)
print(apk)
