"""Verify retained Java/auth/native payloads against the read-only source APK."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def digest(stream):
    sha = hashlib.sha256()
    while block := stream.read(1024 * 1024):
        sha.update(block)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--modified", type=Path, required=True)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--control", action="store_true")
    parser.add_argument("--payload-only", action="store_true")
    args = parser.parse_args()
    profile = json.loads((args.prepared / "patch-report.json").read_text(encoding="utf8"))
    unchanged = []
    with zipfile.ZipFile(args.source) as source, zipfile.ZipFile(args.modified) as modified:
        for entry in source.infolist():
            if not (entry.filename.endswith(".dex") or entry.filename.startswith("lib/")):
                continue
            if entry.filename == "lib/arm64-v8a/libminecraftpe.so" and not (args.control or args.payload_only):
                continue
            with source.open(entry) as a, modified.open(entry.filename) as b:
                if digest(a) != digest(b):
                    raise SystemExit("Unexpected payload change: " + entry.filename)
            unchanged.append(entry.filename)
        with modified.open("lib/arm64-v8a/libminecraftpe.so") as engine:
            if digest(engine) != profile["original_engine_sha256" if args.control or args.payload_only else "modified_engine_sha256"]:
                raise SystemExit("Modified engine does not match prepared profile")
        if modified.read("AndroidManifest.xml") != (args.prepared / "AndroidManifest.xml").read_bytes():
            raise SystemExit("Manifest does not match prepared clone")
        for name in ("libhybridmcpe.so", "libhybrid_bootstrap_check.so"):
            exists = "lib/arm64-v8a/" + name in modified.namelist()
            if exists == args.control:
                raise SystemExit("Missing runtime payload: " + name)
    report = {"unchanged_original_payloads": unchanged, "engine_sha256": profile["original_engine_sha256" if args.control or args.payload_only else "modified_engine_sha256"], "control": args.control, "payload_only": args.payload_only,
              "license_auth_java_and_native_payloads": "unchanged", "live_execution": "not_established"}
    suffix = "control" if args.control else "payload-only" if args.payload_only else "modified"
    (args.prepared / (suffix + "-payload-verification.json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"PASS: {len(unchanged)} original DEX/native payloads unchanged; modified engine profile matches")


if __name__ == "__main__":
    main()
