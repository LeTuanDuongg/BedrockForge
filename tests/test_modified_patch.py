"""Pure parser checks; optional pinned-artifact checks run when research APK exists."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('patch_modified_game', Path(__file__).parents[1] / 'tools/patch_modified_game.py')
patch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patch)


class ModifiedPatchTests(unittest.TestCase):
    def test_wrong_engine_rejected(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            patch.patch_engine(b'not the pinned engine')

    def test_malformed_manifest_rejected(self):
        with self.assertRaises((ValueError, struct.error)):
            patch.patch_manifest(b'not binary XML')

    @unittest.skipUnless(Path('C:/MCPE_Research/base.apk').exists(), 'Pinned research APK unavailable')
    def test_real_engine_dependency_only(self):
        with zipfile.ZipFile('C:/MCPE_Research/base.apk') as apk:
            source = apk.read(patch.ENGINE_ENTRY)
        result = patch.patch_engine(source)
        self.assertEqual(len(source), len(result))
        self.assertEqual(patch.sha(source), patch.ENGINE_SHA)
        self.assertEqual(result[0xaf407b:0xaf407b + len(b'libhybridmcpe.so\0')], b'libhybridmcpe.so\0')
        self.assertEqual(struct.unpack_from('<qQ', result, 0x13934230), (1, 0x7ff3a3))
        self.assertEqual(result[0x13934240:0x13934250], bytes(16))

    @unittest.skipUnless(Path('C:/MCPE_Research/base.apk').exists(), 'Pinned research APK unavailable')
    def test_real_manifest_preserves_code_and_license_strings(self):
        with zipfile.ZipFile('C:/MCPE_Research/base.apk') as apk:
            source = apk.read('AndroidManifest.xml')
        result, removed = patch.patch_manifest(source)
        self.assertEqual(removed, 0)
        self.assertEqual(struct.unpack_from('<I', result, 4)[0], len(result))
        for value in ('dev.hybridmcpe.research', 'com.pairip.application.Application',
                      'com.android.vending.CHECK_LICENSE', 'com.android.vending.BILLING',
                      'android.permission.WRITE_EXTERNAL_STORAGE', 'android.permission.READ_EXTERNAL_STORAGE'):
            self.assertTrue(value.encode('utf-8') in result or value.encode('utf-16le') in result, value)
        # Source string entries remain: only selected attribute references change.
        self.assertTrue('com.mojang.minecraftpe'.encode('utf-16le') in result)
        with self.assertRaisesRegex(ValueError, 'Unexpected original package'):
            patch.patch_manifest(result)

if __name__ == '__main__':
    unittest.main()
