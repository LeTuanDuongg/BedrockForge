"""Offline checks for the debugger tool; these are not Minecraft integration tests."""
import importlib.util
from pathlib import Path
import tempfile
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("registry_trace", ROOT / "tools/lldb_registry_value.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


class RegistryValueTraceTests(unittest.TestCase):
    def test_observed_inline_cobblestone_key(self):
        name = b"minecraft:cobblestone"
        key = (bytes(8) + bytes([len(name) << 1]) + name).ljust(0x28, b"\0")
        self.assertEqual(TRACE.registry_key_name(key, lambda *_: self.fail("unexpected heap read")),
                         name.decode("ascii"))

    def test_heap_name_uses_length_and_pointer(self):
        name = b"minecraft:client_request_placeholder_block"
        key = bytes(8) + struct.pack("<QQQ", 49, len(name), 0x123400) + bytes(8)
        def read(pointer, size):
            self.assertEqual((pointer, size), (0x123400, len(name)))
            return name
        self.assertEqual(TRACE.registry_key_name(key, read), name.decode("ascii"))

    def test_oversized_key_does_not_read_heap(self):
        key = bytes(8) + struct.pack("<QQQ", 201, 100, 0x123400) + bytes(8)
        self.assertIsNone(TRACE.registry_key_name(key, lambda *_: self.fail("unbounded read")))

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
