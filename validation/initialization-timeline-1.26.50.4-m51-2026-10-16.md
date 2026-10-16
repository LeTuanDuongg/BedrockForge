# HybridMCPE / Bedrock 1.26.50.4 initialization timeline

## Result

The initial live M51 trace found an unnecessary 15-second host timer before the diagnostic call. It has been removed. A post-fix M51 run loaded the diagnostic mod about 3.9 seconds after imported Activity `onCreate` returned, with the remaining delay spent on exact engine fingerprint verification. The mod still starts after `android_main`; no vanilla-registration callback is verified.

This is a real-process timeline, not proof of registry finalization or menu/world readiness.

Device: Samsung SM-M515F, Android 12, ARM64; ADB serial `RF8R31WEK5A`. Game process `dev.hybridmcpe.launcher:game`, PID `4849`. Times below are Android `logcat -v monotonic` seconds since boot; each row includes the logcat PID/TID. The main Activity thread is TID `4849`; Minecraft's `android_main` thread is TID `4892`.

| Monotonic time | PID:TID | Observed event |
|---:|---:|---|
| 243356.941 | 4849:4849 | Host classloader resolves imported Minecraft notification class; selected APK/engine profile reports engine SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`. |
| 243356.998 | 4849:4849 | Host's original SDK initialization completes. |
| 243357.035–.043 | 4849:4849 | Android native loader loads `libmaesdk.so`, `libHttpClient.Android.so`, `libPlayFabMultiplayer.so`, and `libfmod.so`. |
| 243357.038 | 4849:4849 | Loader diagnostic stack includes `com.mojang.minecraftpe.MainActivity.<clinit>` while `System.loadLibrary` is active. |
| 243357.125 | 4849:4849 | Android native loader loads `libminecraftpe.so`; Minecraft logs `JNI_OnLoad` entry and completion. This precedes imported Activity `onCreate`. |
| 243357.167 | 4849:4849 | `HostedMinecraftActivity` logs immediately before calling the imported Minecraft `MainActivity.onCreate`. |
| 243357.168 | 4849:4849 | Imported Minecraft `MainActivity::onCreate` begins. |
| 243357.194 | 4849:4849 | Android `GameActivity` looks up `libminecraftpe.so`, finds it, and requests `System.loadLibrary`; the loader already logged that library loaded at `.125`. |
| 243357.196 | 4849:4849 | `GameActivity_register` logs. |
| 243357.196 | 4849:4892 | Minecraft logs `android_main starting`; native engine initialization has entered its game-thread path. |
| 243357.339 | 4849:4849 | Imported Activity `onCreate` returns. This host log explicitly says the menu is not verified. |
| 243357.349 / .397 | 4849:4849 | Minecraft Activity `onStart` / `onResume`. |
| 243357.494 | 4849:4849 | Host `onResume` returns. |
| 243377.130 | 4849:4849 | Mod ABI 1 diagnostic module logs its `start` callback. This is 19.963 seconds after the host's pre-`onCreate` marker and 19.791 seconds after imported `onCreate` returned. |
| 243381.914 | 4849:4849 | Read-only registry snapshot succeeds; `direct_access_pointer=0`. Same-time native diagnostic reports exact engine SHA-256 above, profile `bedrock-1.26.50.4-arm64`, `diagnostic_load=0`, `layout_validated=0`, and `capabilities=0`. |
| 243381.915 | 4849:4849 | Host diagnostic call returns. |

The registry field value of zero is only a field observation. It does not prove the registry is absent, empty, ready, or finalized. The log set contains no timestamped menu or world-loaded callback for this process.

## Startup ownership and ordering

- **Host-controlled:** `HostFactory.instantiateClassLoader` verifies the imported APK set and exact engine hash before launching the Activity. `HostContext` provides the imported assets and isolated profile directories. `HostApplication.onCreate` initializes the host's game SDKs. `HostedMinecraftActivity` controls when the imported Activity is called; after the fix it posts the diagnostic call to the next main-looper turn after `super.onCreate` returns. The earlier 15-second delay is preserved here as the pre-fix measurement.
- **Minecraft-controlled:** the imported `MainActivity` class initializer loads native libraries, including `libminecraftpe.so`; logcat shows its `JNI_OnLoad` before `MainActivity.onCreate`. The APK's `GameActivity.onCreate` later resolves the already-loaded library and calls native `initializeNativeCode(...)`. Live logcat shows `android_main starting` before imported `onCreate` returns. This marks native engine startup, not block-registry finalization.
- **HybridMCPE-controlled:** the posted call initializes/uses the HybridMCPE JNI library, calls `hybrid_probe()` to verify the already-mapped engine, then `hybrid_load_mod()` to load and start the Mod ABI 1 module. The diagnostic is after Minecraft's `initializeNativeCode` path. Its current JNI route is direct; the pinned Preloader adapter is present in the repository but is not initialized or called by this host startup path.
- **Unobserved:** no verified callback reports vanilla block registration start/end or registry finalization. Activity `onCreate`, `onStart`, and `onResume` cannot stand in for those engine events. The app log also does not timestamp the exact menu or world transition.

## Decision

The current Mod ABI 1 module is **too late to participate before native Minecraft initialization begins**. It starts after `android_main` has already begun. Whether a particular vanilla registration operation continues later is unverified; the timeline cannot establish that a registration window remains open.

The earliest host-controlled point is before `HostedMinecraftActivity` calls `super.onCreate` (and earlier, classloader/Application setup). At that point the engine is not yet proven mapped, so the current fail-closed `hybrid_load_mod` path cannot initialize an engine-bound mod there. There is no verified callback between `GameActivity.initializeNativeCode` / `android_main` entry and block-registry finalization. That missing early engine callback is the concrete prerequisite for safe registration work.

The initial measurement used Android monotonic logcat timestamps, PID, and TID. The later code change and device retest are recorded below. No unknown engine function was called; the original Minecraft package and its worlds were not modified.

## Evidence and limits

- Live-device evidence: filtered `adb logcat -d -v monotonic` records for PID `4849`, plus `pidof dev.hybridmcpe.launcher:game` and device enumeration.
- Static host evidence: `HostedMinecraftActivity.java`, `HostFactory.java`, `HostContext.java`, `jni.cpp`, and `native/runtime.cpp` in this repository.
- Imported APK Java evidence: `com.google.androidgamesdk.GameActivity.onCreate` and `com.mojang.minecraftpe.MainActivity.onCreate`, inspected from local APK SHA-256 `7b1ef882ecfec9c41155488e38f69654b95559ab69836d15573cfe2d29232039`. Static decompilation establishes the GameActivity native initialization call; live logcat shows `libminecraftpe.so`/`JNI_OnLoad` before Activity `onCreate` and `android_main` immediately after the GameActivity registration log. The exact native library load caller is supported by the loader's `MainActivity.<clinit>` stack frame; the complete static initializer body was not recovered.
- The privileged process map could not be read from ADB shell (`/proc/4849/maps` returned permission denied). The in-process fingerprint check supplies the exact engine hash instead.
- No exact `System.loadLibrary("hybridmcpe")` timestamp is emitted; source places it in the launcher's `MainActivity` static initializer before its first native diagnostic call. No safe engine-ready, menu-ready, world-ready, or registry-finalized event has been established.

## Post-fix M51 retest

`HostedMinecraftActivity` now posts the diagnostic action to the main looper after `super.onCreate` returns. It no longer waits 15 seconds. The callback logs `host_stage=diagnostic_begin` with PID, TID, and `elapsedRealtimeNanos` before entering JNI. The engine hash checks and capability gates are unchanged.

The ARM64 host APK built successfully with the existing `tools/build.ps1 -PreloaderAdapter` path and installed as an in-place update (`adb install -r`). APK SHA-256: `b6ab67293ad06a1457a0da9ce3592deab55b2ff1ce20f0cb0a94aeeab63147c`. Signer certificate SHA-256: `0f3b814d6cf8d489d0fd936a7ddf3d020841f1845b1e25c94a0361d623dcd9ac`. No Minecraft package or world data was installed, removed, or overwritten.

Post-fix timestamps from the M51 game process PID `16054`:

| Monotonic time | PID:TID | Observed event |
|---:|---:|---|
| 247885.812–.813 | 16054:16054 | Minecraft `JNI_OnLoad` enters and completes. |
| 247885.884 | 16054:16088 | Minecraft `android_main starting`. |
| 247886.056 | 16054:16054 | Imported Activity `onCreate` returns. |
| 247886.233 | 16054:16054 | `diagnostic_begin`; 177 ms after `onCreate` returned. |
| 247889.973 | 16054:16054 | Mod ABI 1 `start`; 3.740 s after diagnostic begins, during native engine SHA-256 verification. |
| 247893.713 | 16054:16054 | Read-only registry snapshot succeeds; `direct_access_pointer=0`; no registration capability. |
| 247893.714 | 16054:16054 | JNI diagnostic returns `diagnostic_load=0`, exact engine hash matches, `capabilities=0`. |
| 247985.270 | 16054:16097 | Minecraft opens the test world database beneath the isolated HybridMCPE version data directory. |
| 248102.845–.846 | 16054:16054 | Activity pauses; diagnostic `stop` and suspend both return success. |
| 248132.402 | 16054:16054 | Same process resumes; diagnostic `start` and `diagnostic_resume=0` succeed. |

The captured device screens show the Minecraft main menu and the isolated `My World` test world. The world was exited through Minecraft's `Save & Quit` control. Two more cold launches started new game processes and loaded the diagnostic successfully; the third process remained alive at the menu. The second run's menu was not separately captured.

| Cold launch | PID | `diagnostic_begin` | Mod ABI 1 `start` | Fingerprint result |
|---:|---:|---:|---:|---|
| 1 | 16054 | 247886.233 | 247889.973 | `diagnostic_load=0`; exact 1.26.50.4 SHA-256; menu and disposable world reached |
| 2 | 18491 | 248412.953 | 248417.084 | `diagnostic_load=0`; exact 1.26.50.4 SHA-256; process alive |
| 3 | 19344 | 248487.837 | 248491.947 | `diagnostic_load=0`; exact 1.26.50.4 SHA-256; menu visible and process alive |

All three cold game processes started the actual Minecraft engine and loaded HybridMCPE once. Menu screens were captured for runs 1 and 3; run 2 had a live process and successful diagnostic logs but no separate menu capture. No fatal-exception or native-crash marker appeared in the captured logs. The first run also entered the isolated world and exited it normally. A separate background/foreground check in PID `16054` reported `diagnostic_suspend=0` and `diagnostic_resume=0`.

The previous trace's `onCreate`-to-mod-start gap was 19.791 seconds. The post-fix gap was 3.917 seconds, a measured reduction of 15.874 seconds. The remaining time is not a timer: it includes exact engine fingerprint hashing. The registry snapshot performs its existing second verification; this change does not cache or skip hashes.

## Decision after retest

HybridMCPE can initialize its diagnostic module earlier by removing the arbitrary delay, but only after imported Activity `onCreate` returns and the engine library is mapped. The post-fix callback is still after `JNI_OnLoad` and `android_main` entry. It does not prove that vanilla registration remains open, and the registry snapshot still reports a zero direct-access pointer. Earlier host scheduling does not supply a verified registration opportunity.

The specific missing requirement remains an engine-owned callback or documented/verified registration interface that identifies the block-registration phase, its thread, ABI, ownership, and finalization boundary. Do not register blocks or enable gameplay capabilities until that contract is established.
