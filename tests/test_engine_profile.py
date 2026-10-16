"""Checks against the actual pinned engine, not a simulated gameplay implementation."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("inspect_engine", ROOT / "tools/inspect_engine.py")
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


class EngineProfileTests(unittest.TestCase):
    def test_rejects_wrong_architecture(self):
        with self.assertRaises(ValueError):
            engine.inspect(b"not an ELF file" + bytes(64))

    def test_actual_target_chest_and_transactions(self):
        profile = engine.inspect(Path("C:/MCPE_Research/libminecraftpe.so").read_bytes())
        self.assertEqual(profile["sha256"], engine.TARGET_SHA256)
        classes = profile["rtti"]
        self.assertEqual(len(classes), 9)
        chest = classes["21ChestScreenController"]["vtables"][0]
        self.assertEqual(chest["vtable_address_point"], "0x12ab50a8")
        self.assertEqual(chest["slots"][0], "0x86232d4")
        for name in ("27ComplexInventoryTransaction", "27ItemUseInventoryTransaction", "26InventoryTransactionPacket"):
            self.assertTrue(classes[name]["vtables"])


if __name__ == "__main__":
    unittest.main()
