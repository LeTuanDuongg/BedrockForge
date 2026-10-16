# HybridMCPE

Native-first Minecraft Bedrock Android mod runtime. Target: Samsung M51, Android 12, ARM64; Minecraft **1.26.52.3**.

## Modified-game bootstrap status: NO-GO

Run `tools/build-modified.ps1` to produce local-only `dist/HybridMCPE-local-game.apk` for package `dev.hybridmcpe.research`. Do not distribute the APK or copied game libraries. No root, Frida or Levi host is used by this route.

The version-pinned patcher adds `DT_NEEDED=libhybridmcpe.so` in existing ELF metadata/padding and signs a separate application. `.text`, `.rodata`, relocations, addresses and file length remain unchanged for this particular patch. Future HybridMCPE engines need not remain unmodified: the existing runtime receives a generated exact hash/size profile for the modified engine.

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

## Preserved original-profile build

`tools/build.ps1` builds the C++ runtime, native mod library, native tests and signed `dist/HybridMCPE-debug.apk` using the installed Android SDK/NDK. Tool locations are parameters. No Gradle download is required.

The launcher checks the installed engine SHA-256 and packages the native payload. **It does not inject into Minecraft.** Its button explicitly opens Minecraft with mods inactive.

The native runtime runs inside an existing engine process. It checks ELF identity, the full engine file SHA-256, and five RTTI/vtable layouts before allowing plugin loading. `sdk/hybrid_mod.h` defines the C ABI; plugin loading uses `dlopen`, validates ABI/capabilities and handles lifecycle shutdown. This is not a gameplay SDK yet.

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

`tools/build.ps1 -PreloaderAdapter` compiles `bootstrap/preloader/adapter.cpp` into the existing runtime and emits `dist/hybridmcpe-bootstrap.levipack`. The adapter exports the verified private C lifecycle contract (`PLMod_Load`, `PLMod_Enable`, `PLMod_Disable`, `PLMod_Unload`), calls the existing probe/shutdown functions and leaves the independent Hybrid Mod ABI unchanged. No Preloader source, SDK implementation or binary is vendored. No gameplay functions are invoked.

Load requires the real engine already mapped, the original SHA-256 and all five existing layout checks. Missing engine causes failure, not a successful bootstrap. Diagnostic logs include PID, process name and runtime status. The adapter callbacks, invalid metadata rejection and absent-engine rejection ran on M51 and passed; existing native tests also passed again. This was a test process, **not** live Minecraft. Mod start/stop inside the engine remains untested.

Evaluated backend: LeviLaunchroid v1.5.27, with Preloader commit `5d108da350ce3e7f13f51415f81e65344ecd5d14`. It was not installed or launched: its hosted Google Play store uses a `StubStore` that unconditionally reports a verified license, and its `NativeImageGuard` can rewrite cached engine files. That does not meet this project's no-bypass requirement and verified-target policy. No license file was found in the pinned standalone Preloader tree; the launcher's Apache-2.0 license alone does not establish the submodule's terms. Source links and deployment rejection are recorded in `bootstrap/preloader/compatibility.json`.

The Preloader package remains a preserved adapter artifact, not the primary deployment route. Its compatibility record documents the rejected backend; it is not an enforcement mechanism in third-party launchers. The modified-game route now uses its own generated engine profile instead of requiring unchanged engine bytes.

## Remaining work

Research version-specific Container/Inventory contracts from the existing Ghidra project. Build fail-closed native Engine Bridge foundations only around verified contracts. Registration, 54-slot ownership, transactions, chest UI, touch interaction and persistent storage remain unverified and unavailable; Iron Chest has not passed in-game testing.

All source research artifacts under `C:/MCPE_Research` were treated as read-only. No previous Bedrock Forge project is used.
