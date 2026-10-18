# HybridMCPE

Native-first Minecraft Bedrock Android mod runtime. Device: Samsung M51, Android 12, ARM64. Current real-host target: **Minecraft 1.26.50.4**; all **1.26.52.3** profiles and research are preserved.

## Imported-engine host: verified on M51

The existing launcher now imports installed Minecraft APK/splits or user-selected APK files without requiring a Google Play installation source. Imports verify cryptographic signatures, identify signers, detect version/versionCode, extract ARM64 libraries and select exact engine profiles. Unknown publishers remain explicitly untrusted; a valid signature is not proof of official publication or licensing.

The host loads the original imported MainActivity, Java SDKs, assets and native engine in `dev.hybridmcpe.launcher:game`, with version-specific private storage paths. It does not replace license, authentication or integrity responses. No proprietary Minecraft code or engine is packaged in HybridMCPE. Versions without a known exact runtime profile remain unavailable for execution.

**Real M51 result, 2026-10-16:** the exact 1.26.50.4 engine reached the menu and a local world. PID **10830** loaded the existing runtime and the bundled **Mod ABI 1 diagnostic** module, matched the engine hash, and logged successful native start/stop/resume through recents switching. The diagnostic run remained alive for more than five minutes. The installed original Minecraft APK hash remained unchanged. See [device evidence](validation/embedded-host-m51-2026-10-16.md).

Build with `tools/build.ps1 -PreloaderAdapter`, install only `dist/HybridMCPE-debug.apk`, then select **Dùng Minecraft đang cài** or import base plus required split APKs. The engine-only button is the first startup test. After confirming its menu, the diagnostic button loads the bundled native diagnostic module. No personal account or existing world is required. General third-party module discovery and gameplay bindings remain unfinished; Iron Chest does not work in-game yet.

## Modified-game bootstrap status: NO-GO

Run `tools/build-modified.ps1` to produce local-only `dist/HybridMCPE-local-game.apk` for package `dev.hybridmcpe.research`. Do not distribute the APK or copied game libraries. No root, Frida or Levi host is used by this route.

The version-pinned patcher adds `DT_NEEDED=libhybridmcpe.so` in existing ELF metadata/padding and signs a separate application. `.text`, `.rodata`, relocations, addresses and file length remain unchanged for this particular patch. Runtime profile selection now accepts only the exact original or exact metadata-patched engine hashes; the patched engine is not a general runtime requirement.

An ELF constructor initializes the existing runtime synchronously. A linker export map prevents static C++ symbols from interposing on the game. All original DEX/native payloads except the deliberately patched engine are retained, including Pairip and MCPELIFE, all original permissions, licensing, purchases and authentication code. Only package identity, package-owned permissions and provider authorities are renamed in the manifest.

**Actual M51 result, 2026-10-16:** PID **7312** logged the mapped engine, matching modified SHA-256, five validated RTTI/vtable layouts, mod ABI 1 start/stop, duplicate rejection `-6` and completed native lifecycle. No recovered engine functions were called. This is native execution in the research game process during startup, not an interactive-world test.

The modified package is preserved for research, but its deployment route is **NO-GO** until a supported, authorized startup path is available. The M51 matrix contains:

- A, original package and signer: existing process `13778` stayed alive through a read-only 5 minute 4 second observation; installed APK SHA-256 matched before and after. This was not a cold launch.
- B, renamed/re-signed clone with original engine and no Hybrid payload: `System.exit(0)` and process restart.
- C, same clone with both Hybrid libraries packaged but not loaded: same exit.
- D, modified engine loading Hybrid: runtime and mod lifecycle complete in-process, then the same exit.

B and C show that loading HybridMCPE is not required to reproduce the exit. A versus B changes package identity, package-owned manifest references and signer together, so these tests do not isolate the cause. Each clone logs `System.exit(0)` after `JNI_OnLoad`; no fatal crash is logged. The `<queries>` parser warning is also observed, but its presence in the original manifest means it is not established as the cause. Pairip integrity runs before licensing in the inspected startup path, but these logs do not identify either check as the caller or cause of the exit. The local clone certificate differs from the original certificate. Treat protection/signature compatibility as unresolved, not a confirmed DRM finding.

The `System.exit` caller remains unknown. The separate debug APK has `android:debuggable=true`; the installed D APK does not, and Package Manager reports D as non-debuggable. The historical Package Manager probe for the debug APK has no matching installed-APK hash, so it does not establish a same-artifact contradiction. No termination call is suppressed and no license, integrity or authentication check is changed.

The test runner records the APK hash, signing certificate, launch status, last observed stage, exit lines and timestamped logcat under `build/modified/device/<variant>-result.json` and `<variant>-full.log`. Installed B, C and D APK hashes were rechecked against their local signed artifacts; D was restored. The original APK hash remains unchanged. See `build/modified/device/A-original-five-minute-result.json` and `apk-installed-hash-matrix.json` for this milestone's device evidence.

Do not repeat B/C/D packaging experiments without a new testable hypothesis. `--diagnostic` retains the observed exit status and does not suppress it.

The original installed APK was backed up to `build/backups/installed-minecraft-base.apk` and its installed hash verified unchanged after tests. The original app was not removed or replaced. Private worlds are inaccessible to non-root ADB and were not backed up, transferred or loaded for testing. Build outputs and backups are ignored by Git.

Modified engine SHA-256: `a8942061e3743fce10952627c76615c9e5c0cb07b897efc33f8dabaec96bf4d6`. Source and modified profiles are kept separately. Python patch/profile checks, signed APK/alignment checks and M51 native runtime guards pass. Installation and native lifecycle do not establish game/world stability.

## Runtime build and compatibility

`tools/build.ps1` builds the C++ runtime, native mod library, native tests and signed `dist/HybridMCPE-debug.apk` using the installed Android SDK/NDK. Tool locations are parameters. No Gradle download is required.

The launcher copies source APKs into its private version repository and creates an imported-engine process through Android's AppComponentFactory and ResourcesLoader APIs. It does not inject into or overwrite the separately installed Minecraft application. The [apksig dependency](android/APKSIG.md) and import-only tests document signature validation and distribution boundaries.

The native runtime selects an exact full-SHA-256, size and AArch64 match from `native/engine_profiles.h`. The two preserved 1.26.52.3 profiles require all five recorded layout checks. The exact 1.26.50.4 profile retains zero legacy gameplay capabilities; its SDK now separately admits read-only Block lookup after additional registry/getter checks. There are no wildcard fingerprints or fallback layouts. Importing an unknown fingerprint does not enroll it in this trusted runtime table. `sdk/hybrid_mod.h` defines the unchanged Mod ABI 1; plugin loading uses `dlopen`, validates ABI/capabilities and handles lifecycle shutdown.

`mods/iron_chest.cpp` is a blocked native mod descriptor, not a working chest. Container, GUI and persistence capabilities are all unavailable. Even if those flags were mistakenly enabled, its start function fails until real registration code exists. No entity-inventory substitute or simulated game backend is included.

## Verified target

Engine SHA-256: `53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a`.

APK SHA-256: `b618ac8afda57ec19e5614afbabdc85765d10224dbdc1ec47315a94243e2fdb9`.

`targets/bedrock-1.26.52.3-arm64.json` records relocation-backed RTTI/vtable candidates for nine inventory, transaction and UI classes. These are observed ELF virtual addresses, not invented function signatures. Native profile validation currently covers five classes. Ghidra uses an artificial `0x100000` image base; runtime addresses use the actual loader bias instead.

`tools/inspect_engine.py` regenerates the target profile read-only. `tools/ghidra/` inspects a copied project and decompiles candidate vtable entries. Existing analysis was accessed using Ghidra 12.1.4 directly: REA 6.5.0's Windows Ghidra bridge rejects ARM64 ELF. Pseudocode has unresolved types, indirect calls and PIC warnings; method purposes and calling signatures remain unverified. Do not call or hook these addresses yet.

## Earlier device validation — original-profile tooling

- M51 connected over USB; Android 12, `arm64-v8a`, Minecraft 1.26.52.3 confirmed.
- Signed launcher installed and opened successfully; displayed engine hash matches the target.
- `hybrid_self_tests` executed on M51: SHA-256 vectors and memory-bound checks passed. Pointer-layout tests use test memory, not a running engine.
- `hybrid_native_tests` executed on M51: absent-engine rejection, path validation and shutdown passed.
- Minecraft launched, but native injection was not achieved. `run-as` reports the game is not debuggable; `su` was absent from PATH. REA/Frida attach failed: `Unable to connect to remote frida-server: closed`.

The earlier Frida tooling remains preserved but is not the selected bootstrap route. No further Frida attachment was attempted during the Preloader adapter work.

## Replaceable bootstrap adapter

`tools/build.ps1 -PreloaderAdapter` compiles `bootstrap/preloader/adapter.cpp` into the existing runtime and emits `dist/hybridmcpe-bootstrap.levipack`. Its `PLGetModRegistration` table is pinned to the Preloader submodule commit used by LeviLaunchroid v1.5.24 (`7ca94daedfa30d6d4c265fc9b591768e8dc1f5af`). The adapter keeps `ModContext` opaque, maps the four host callbacks to HybridMCPE probe, mod suspend/resume and shutdown operations, and leaves Hybrid Mod ABI 1 unchanged. No Preloader source or SDK header is vendored; no gameplay functions are invoked.

Load requires a mapped engine matching an exact supported profile and its required layout checks. Missing or unsupported engines fail closed. Diagnostic logs include PID, process name and runtime status. The isolated ARM64 ABI harness validates registration, lifecycle dispatch, rejection paths and cleanup; it is not a live Preloader test. The separate imported-engine JNI bootstrap has now demonstrated native mod start/stop/resume in real Minecraft; live Preloader deployment remains untested.

Evaluated ABI target: LeviLaunchroid v1.5.24, whose Preloader submodule is pinned to `7ca94daedfa30d6d4c265fc9b591768e8dc1f5af`. This verifies the adapter contract, not game-host compatibility. The release notes mention Minecraft 1.26.50, not the 1.26.52.3 target. The release was not installed or launched; no license-preserving and integrity-safe host configuration has been established. The pinned Preloader tree has no LICENSE file, and no upstream implementation or header is copied. Source links and deployment status are in `bootstrap/preloader/compatibility.json`.

The Preloader package remains a preserved adapter artifact, not the primary deployment route. Its compatibility record documents the rejected backend; it is not an enforcement mechanism in third-party launchers. The modified-game route now uses its own generated engine profile instead of requiring unchanged engine bytes.

## Remaining work

Research version-specific Container/Inventory contracts from the existing Ghidra project. Build fail-closed native Engine Bridge foundations only around verified contracts. Registration, 54-slot ownership, transactions, chest UI, touch interaction and persistent storage remain unverified and unavailable; Iron Chest has not passed in-game testing.

All source research artifacts under `C:/MCPE_Research` were treated as read-only. No previous Bedrock Forge project is used.

## Native SDK status

The existing `HybridSdkV1` provides lifecycle events, dependency manifests, logging and read-only `blocks.find` on exact 1.26.50.4 ARM64. The public SDK example returned real `minecraft:cobblestone` default-state metadata on M51 ([evidence](validation/sdk-gameplay-2026-10-18.md)). Custom registration, Player/World, commands, items, inventories, entities, GUI and rendering remain unsupported. Run `tools/test-host.ps1` without ADB. See [SDK coverage and examples](docs/native-sdk.md).
