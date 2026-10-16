import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class EngineContractTests(unittest.TestCase):
    def test_unverified_candidates_never_enable_gameplay_capabilities(self):
        record = json.loads((ROOT / "targets/bedrock-1.26.52.3-arm64-engine-contracts.json").read_text())
        self.assertEqual(record["sha256"], "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a")
        self.assertTrue(record["candidates"])
        self.assertTrue(all(candidate["status"] == "research_only" for candidate in record["candidates"]))
        self.assertEqual(
            record["unavailable_capabilities"],
            ["container", "chest_ui", "persistence"],
        )


if __name__ == "__main__":
    unittest.main()
