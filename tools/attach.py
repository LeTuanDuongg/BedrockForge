"""Load HybridMCPE inside an existing Minecraft process through an authorized Frida device."""
import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="Exact Frida device ID")
    parser.add_argument("--runtime", required=True, help="Absolute device path to libhybridmcpe.so")
    parser.add_argument("--mod", required=True, help="Absolute device path to libiron_chest.so")
    args = parser.parse_args()
    try:
        import frida
        device = frida.get_device(args.device, timeout=5)
        matches = [app for app in device.enumerate_applications() if app.identifier == "com.mojang.minecraftpe"]
        if len(matches) != 1 or not matches[0].pid:
            raise RuntimeError("Start the owned Minecraft installation first; no running process found")
        with device.attach(matches[0].pid) as session:
            script = session.create_script((Path(__file__).parent / "frida/bootstrap.js").read_text(encoding="utf8"))
            script.load()
            report = script.exports_sync.bootstrap(args.runtime, args.mod)
            print(json.dumps(report, indent=2))
            # Until engine bindings exist, the mod must remain rejected.
            if report["result"] != -5:
                raise RuntimeError("Unexpected activation result; gameplay must remain blocked")
            script.exports_sync.shutdown()
    except Exception as error:
        print(f"HybridMCPE attach failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
