"""Check the distribution boundary: the host must not ship Minecraft implementations."""
import struct
import unittest
import zipfile
from pathlib import Path


def defined_classes(dex):
    strings_count, strings_offset = struct.unpack_from("<II", dex, 56)
    types_count, types_offset = struct.unpack_from("<II", dex, 64)
    classes_count, classes_offset = struct.unpack_from("<II", dex, 96)
    strings = []
    for i in range(strings_count):
        offset = struct.unpack_from("<I", dex, strings_offset + i * 4)[0]
        while dex[offset] & 0x80:
            offset += 1
        offset += 1
        strings.append(dex[offset:dex.index(b"\0", offset)].decode("utf-8", errors="replace"))
    types = [strings[struct.unpack_from("<I", dex, types_offset + i * 4)[0]] for i in range(types_count)]
    return {types[struct.unpack_from("<I", dex, classes_offset + i * 32)[0]] for i in range(classes_count)}


class LauncherDistributionTests(unittest.TestCase):
    def test_host_contains_no_game_implementation(self):
        root = Path(__file__).resolve().parents[1]
        with zipfile.ZipFile(root / "dist/HybridMCPE-debug.apk") as apk:
            names = apk.namelist()
            classes = set()
            for name in names:
                if name.startswith("classes") and name.endswith(".dex"):
                    classes.update(defined_classes(apk.read(name)))
            self.assertIn("Ldev/hybridmcpe/launcher/HostedMinecraftActivity;", classes)
            self.assertIn("Lcom/android/apksig/ApkVerifier;", classes)
            self.assertFalse(any(c.startswith("Lcom/mojang/") for c in classes), "compile-only API leaked into release")
            self.assertFalse(any(c.startswith("Lcom/android/apksigner/") for c in classes))
            self.assertNotIn("lib/arm64-v8a/libminecraftpe.so", names)
            self.assertIn("lib/arm64-v8a/libhybrid_diagnostic.so", names)
            self.assertIn("assets/third-party/apksig-NOTICE.txt", names)


if __name__ == "__main__":
    unittest.main()
