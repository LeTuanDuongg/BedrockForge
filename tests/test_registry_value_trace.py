"""Offline checks for the debugger tool; these are not Minecraft integration tests."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("registry_trace", ROOT / "tools/lldb_registry_value.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


class RegistryValueTraceTests(unittest.TestCase):
    def test_top_byte_tag_preserves_address(self):
        self.assertEqual(TRACE.canonical_pointer(0xAB0000762B162DC0), 0x762B162DC0)

    def test_wrong_binary_is_rejected_before_elf_parsing(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            file = Path(folder) / "wrong.so"
            file.write_bytes(b"not the target engine")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                TRACE.engine_layout(file)

    def test_pinned_real_elf_and_writer(self):
        elf = ROOT / "build/target-apk-analysis/26.50.4/libminecraftpe.so"
        if not elf.is_file():
            self.skipTest("local research ELF unavailable")
        segments, writer = TRACE.engine_layout(elf)
        self.assertEqual(writer.hex(), "f50200f9")
        self.assertTrue(any(va <= TRACE.REGISTRY_VA < va + size and flags & 2
                            for va, size, flags, _, _ in segments))


if __name__ == "__main__":
    unittest.main()
