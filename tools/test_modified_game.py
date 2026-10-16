"""Deploy only the isolated research package; preserve the installed game and collect evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.hybridmcpe.research"
ORIGINAL = "com.mojang.minecraftpe"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--launch-only", action="store_true", help="Retest the existing research install without reinstalling")
    parser.add_argument("--apk", type=Path, default=ROOT / "dist/HybridMCPE-local-game.apk")
    parser.add_argument("--control", action="store_true", help="Assert stability of the clone without HybridMCPE")
    parser.add_argument("--label", help="Evidence variant label, e.g. B-control or C-payload-only")
    parser.add_argument("--diagnostic", action="store_true", help="Record an unstable startup as evidence and return success")
    parser.add_argument("--baseline-original", action="store_true", help="Observe original package without installing or force-stopping it")
    parser.add_argument("--observe-seconds", type=int, default=30, help="Observe the launch window before collecting logs")
    parser.add_argument("--adb", default="C:/Users/letua/AppData/Local/Android/Sdk/platform-tools/adb.exe")
    args = parser.parse_args()
    if args.observe_seconds < 1:
        parser.error("--observe-seconds must be positive")
    evidence = ROOT / "build/modified/device"
    evidence.mkdir(parents=True, exist_ok=True)

    def adb(*commands, timeout=40, check=True):
        result = subprocess.run([args.adb, "-s", args.serial, *commands], capture_output=True,
                                text=True, encoding="utf8", errors="replace", timeout=timeout)
        if check and result.returncode:
            raise RuntimeError(" ".join(commands) + ": " + result.stdout + result.stderr)
        return result.stdout + result.stderr

    original_path = adb("shell", "pm", "path", ORIGINAL).strip().removeprefix("package:")
    if "\n" in original_path or not original_path.endswith("/base.apk"):
        raise RuntimeError("Expected the verified single-APK installed target")
    original_sha = adb("shell", "sha256sum", original_path).split()[0]
    backup = ROOT / "build/backups"
    if not args.launch_only:
        backup.mkdir(parents=True, exist_ok=True)
        research = Path("C:/MCPE_Research/base.apk")
        with research.open("rb") as source:
            source_sha = hashlib.file_digest(source, "sha256").hexdigest()
        if original_sha == source_sha:
            shutil.copyfile(research, backup / "installed-minecraft-base.apk")
        else:
            adb("pull", original_path, str(backup / "installed-minecraft-base.apk"), timeout=120)
    package = ORIGINAL if args.baseline_original else PACKAGE
    activity = package + "/com.mojang.minecraftpe.MainActivity"
    apk = Path("C:/MCPE_Research/base.apk") if args.baseline_original else args.apk
    apk_sha = hashlib.file_digest(apk.open("rb"), "sha256").hexdigest() if apk.is_file() else None
    signer = None
    apksigner = Path("C:/Users/letua/AppData/Local/Android/Sdk/build-tools/36.0.0/apksigner.bat")
    if apk.is_file() and apksigner.is_file():
        signed = subprocess.run([str(apksigner), "verify", "--print-certs", str(apk)], capture_output=True,
                                text=True, encoding="utf8", errors="replace", timeout=30)
        match = re.search(r"Signer #1 certificate SHA-256 digest: ([0-9a-fA-F]+)", signed.stdout + signed.stderr)
        signer = match.group(1).lower() if match else None
    label = args.label or ("A-original" if args.baseline_original else "B-control" if args.control else "D-bootstrap")
    report = {"variant": label, "serial": args.serial, "package": package,
              "apk_sha256": apk_sha, "signing_certificate_sha256": signer,
              "original_apk_sha256": original_sha,
              "original_apk_backup": str(backup / "installed-minecraft-base.apk"),
              "worlds": "Original app retained; private worlds are inaccessible to non-root ADB and are not copied.",
              "signing_configuration": "original installed signer" if args.baseline_original else "local HybridMCPE research key"}
    if not args.baseline_original:
        adb("shell", "am", "force-stop", PACKAGE, check=False)
    if not args.launch_only and not args.baseline_original:
        report["install"] = adb("install", "--no-incremental", "-r", str(args.apk), timeout=120).strip()
    before = adb("shell", "pidof", package, check=False).split()
    since = adb("shell", "date", "+%s").strip() + ".000"
    report["launch"] = adb("shell", "am", "start", "-W", "-n", activity, timeout=60, check=False).strip()
    report["preexisting_pids"] = before
    pids = set()
    for _ in range(args.observe_seconds):
        pids.update(adb("shell", "pidof", package, check=False).split())
        if len(pids) > 1:
            break
        time.sleep(1)
    report["final_pid"] = adb("shell", "pidof", package, check=False).strip()
    logs = adb("logcat", "-b", "all", "-d", "-v", "threadtime", "-T", since)
    pids.update(re.findall(r"Process: " + re.escape(package) + r", PID: (\d+)", logs))
    stem = label
    (evidence / (stem + "-full.log")).write_text(logs, encoding="utf8")
    lines = []
    for line in logs.splitlines():
        columns = line.split()
        if package in line or (len(columns) > 2 and columns[2] in pids):
            lines.append(line)
    filtered = "\n".join(lines) + "\n"
    (evidence / (stem + ".log")).write_text(filtered, encoding="utf8")
    report["observed_pids"] = sorted(pids)
    report["runtime_constructor_observed"] = "event=dependency_constructor" in filtered
    report["mapped_engine_observed"] = "event=engine_mapped" in filtered
    report["mod_lifecycle_observed"] = all(event in filtered for event in ("event=mod_start", "event=mod_stop", "event=lifecycle_complete"))
    events = {}
    for line in lines:
        match = re.search(r"event=(\w+) pid=(\d+)", line)
        if match:
            events.setdefault(match[2], set()).add(match[1])
    report["same_process_mapping_pids"] = sorted(pid for pid, names in events.items()
        if {"dependency_constructor", "engine_mapped"} <= names)
    report["fingerprint_profile_verified"] = any("event=fingerprint" in line and
        "engine_verified:1.26.52.3;layout_validated=5;" in line for line in lines)
    report["stable_process"] = len(pids) == 1 and report["final_pid"] in pids
    report["system_exit_lines"] = [line for line in lines
        if "System.exit called" in line or "VM exiting with result code" in line]
    report["last_successful_stage"] = "HybridMCPE lifecycle complete" if report["mod_lifecycle_observed"] else (
        "Minecraft JNI_OnLoad complete" if any("JNI_OnLoad completed" in line for line in lines) else
        "Minecraft native library mapped" if any("libminecraftpe.so" in line for line in lines) else "Android activity launch only")
    report["exit_reason"] = "System.exit(0) observed; Java caller not captured" if report["system_exit_lines"] else (
        "process death/restart observed; cause not logged" if len(pids) > 1 else "not established")
    report["original_install_unchanged"] = adb("shell", "sha256sum", original_path).split()[0] == original_sha
    passed = report["stable_process"] and (args.control or args.baseline_original or (
        report["mod_lifecycle_observed"] and report["fingerprint_profile_verified"] and
        report["final_pid"] in report["same_process_mapping_pids"]))
    if not passed:
        if not args.baseline_original:
            adb("shell", "am", "force-stop", PACKAGE)
        report["research_clone_stopped_after_failure"] = True
    if passed:
        adb("shell", "uiautomator", "dump", "/data/local/tmp/hybridmcpe-modified-ui.xml", check=False)
        ui = adb("shell", "cat", "/data/local/tmp/hybridmcpe-modified-ui.xml", check=False)
        if package in ui:
            (evidence / "ui.xml").write_text(ui, encoding="utf8")
            report["ui_text"] = [text for text in re.findall(r'text="([^"]*)"', ui) if text][:30]
    (evidence / (stem + "-result.json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps(report, indent=2))
    return 0 if passed or args.diagnostic else 1


if __name__ == "__main__":
    sys.exit(main())
