import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"sdk/scripting"))
from bedrockforge import Host, ModError
LIBRARY = sys.argv.pop(1)
class ScriptingTests(unittest.TestCase):
    def test_native_core_binding(self):
        with tempfile.TemporaryDirectory() as root:
            with Host(LIBRARY,root) as host:
                api=host.mod("script")
                api.register_item("script:gem","Scripting Gem","demo")
                self.assertEqual(len(api.search("gem")),1)
                other=host.mod("another")
                self.assertEqual(len(other.search("gem","demo")),1)
                with self.assertRaises(ModError): other.register_item("script:gem","Spoof")
                c=api.container("chest",108)
                api.deposit(c,107,"script:gem",12)
                self.assertEqual(api.read(c,107)["count"],12)
                with self.assertRaises(ModError): api.deposit(c,107,"script:gem",1)
                self.assertFalse(api.capability("game.inventory.v1"))
                with self.assertRaises(ValueError): api.read(c,-1)
            with self.assertRaises(ModError): api.search()
            # Reload script inventory after providers register again.
            with Host(LIBRARY,root) as host:
                api=host.mod("script")
                api.register_item("script:gem","Scripting Gem","demo")
                c=api.container("chest",108)
                self.assertEqual(api.read(c,107)["count"],12)
                provider=host.mod("provider")
                provider.register_item("provider:gem","Gem")
                self.assertFalse(provider.capability("game.ui.v1"))
if __name__ == "__main__": unittest.main()
