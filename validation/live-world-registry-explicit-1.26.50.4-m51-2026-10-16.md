# Explicit live-world registry probe: verified M51 observation

Target: Minecraft Bedrock 1.26.50.4 ARM64 on Samsung SM-M515F, Android 12. All times below are device time, UTC+07:00, 2026-10-16.

## Result

The experimental native module performed an explicitly triggered, bounded registry memory sample after a gameplay HUD was visually confirmed. Four existing entries have namespaced vanilla identifiers and matching concrete block RTTI. This confirms a real engine-owned tree containing vanilla block objects. It does not establish that those objects are `BlockDefinition` documents, a callable lookup ABI, or a registration contract.

The original installed Minecraft package, accounts, and personal worlds were not changed. Only the existing HybridMCPE host was updated. The user confirmed `My World` in its isolated profile is disposable; it was saved and quit normally after the test.

## Reproduction and restrictions

1. Build with NDK 28.2.13676358 using `tools/build.ps1 -ExperimentalNative -PreloaderAdapter` (engine hooks remain OFF).
2. Select the imported 1.26.50.4 profile and launch the experimental passive probe mode.
3. Open the disposable world. On this device a 120 ms stationary touch activated the world card reliably. Wait for the actual gameplay HUD; a loading screen is insufficient.
4. Capture and visually inspect a screenshot **before** issuing the fixed command:

```text
adb shell am broadcast --user 0 -a dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE -p dev.hybridmcpe.launcher
```

There is no startup timer or automatic registry sample. The receiver exists only when the native experimental build flag and experimental host mode are both enabled. Android's registered receiver requires the sender to hold `android.permission.DUMP`; device broadcast state confirmed this permission and shell UID 2000 as sender. No address or argument is accepted. The JNI entry resolves only the already-loaded experimental module's fixed diagnostic symbol using `RTLD_NOLOAD`. Normal stable builds return false for experimental availability and `-4` for the trigger. Mod ABI 1 is unchanged.

The Android main thread only schedules the diagnostic. A separate worker uses `process_vm_readv` on its own process and invokes **no engine functions**. It follows at most four nodes through the previously observed child fields, double-reads each node, checks the owner and root again, and bounds RTTI reads to the verified engine mapping. This is a best-effort snapshot without an engine lock, not a game-thread callback or an atomic world snapshot. Paused/background/destroyed host states are rejected. A successful trigger is consumed for the module lifetime, including stop/resume; stop joins the worker before unloading can occur.

## Exact artifacts and timing

- Installed host package: `dev.hybridmcpe.launcher`; game process: `dev.hybridmcpe.launcher:game`, PID **18936**.
- Host APK SHA-256: `c2b90f49b6a6b76558826f5c643492a5ac09423c6848d5de64c557306dbfc721`. The device's installed `base.apk` SHA-256 matched the tested `dist/HybridMCPE-explicit-probe-debug.apk`.
- Runtime library SHA-256: `ddff57b4d271d6c597e382ba7cf3f08bd5d9aab9b790e97085dd60d5b2b3a160`.
- Experimental module SHA-256: `4f63606ed6ca0aadd129f8740a9a89a4eff56a6c540fb89cef88d8e1716be5c8`.
- Engine SHA-256, revalidated by the native worker: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Getter returned registry **0x7631567db8** at **23:35:55.089**. The later sample used the same registry identity, RVA `0x134d7db8`.
- HUD confirmed at **23:45:48.453525116**, before any trigger. Evidence: [before HUD](live-world-hud-before-trigger-2026-10-16.png).
- Trigger request: **23:46:42.826**, receiver TID **18936**, elapsed realtime **425661932727322 ns**.
- Execution begins: **23:46:42.835**, worker TID **21340**, elapsed realtime **425661943425031 ns**.
- Sample: **23:46:48.214**; completion: **23:46:48.215**, elapsed realtime **425667322956226 ns**.
- HUD still visible at **23:46:53.363525091**, same PID. Evidence: [after HUD](live-world-hud-after-trigger-2026-10-16.png). The game responded to Back by opening its real pause screen: [responsive UI](live-world-responsive-after-trigger-2026-10-16.png).
- Second trigger at **23:49:06.122** returned **-2** without another execution. After Save & Quit and Home, lifecycle suspend returned **0**; background trigger at **23:52:57.860** returned **-1**.

Raw native evidence: [corrected reader run](live-world-registry-tagfix-2026-10-16.txt). The formatted Android log truncated the final snapshot qualifiers, but retained all four nodes. The function returns its successful result only after owner/root revalidation; the capture is still best-effort, not a lifetime guarantee.

## Observed storage and key representation

Registry root at `registry+8`: **0xb400007706a1be90**. These are tagged heap pointers, not ELF-relative addresses. Child links remain at node `+0x00` and `+0x08`; mapped value is at `+0x48`.

| Node | Identifier decoded from raw bytes | Prefix u64 at +0x20 | Value pointer at +0x48 | Validated RTTI name |
|---|---|---|---|---|
| 0 | `minecraft:unknown` | `0x736010f981f0ae5a` | `0xb4000077b6d078c0` | `12UnknownBlock` |
| 1 | `minecraft:cobblestone` | `0x3e53526998bb823e` | `0xb4000077b6d11ca0` | `16CobblestoneBlock` |
| 2 | `minecraft:bedrock` | `0xc76db5e0e15a7c56` | `0xb4000077b6d0f180` | `12BedrockBlock` |
| 3 | `minecraft:stone_button` | `0x21fa9a2dc78c1ece` | `0xb4000077b6d1f890` | `11ButtonBlock` |

The value's vptr, typeinfo, and type-name pointers were checked against the exact engine image before accepting RTTI text. No virtual method was called. The concrete names associate these entries with vanilla block objects; they do not prove `BlockDefinition` inheritance or ownership semantics.

All four keys have an eight-byte prefix at node `+0x20`, followed by a short-string-compatible representation at `+0x28`: descriptor bytes `0x22`, `0x2a`, `0x22`, `0x2c` encode lengths 17, 21, 17, 22 using `byte >> 1`; ASCII begins at `+0x29` and each string is null terminated at that length. This contradicts treating the key at `+0x20` as a plain `std::string`. The prefix's semantic type/hash algorithm, long-key layout, and exact comparator argument contract remain unverified. Bare `minecraft` was not observed in this four-node sample; neither it nor `minecraft:stone` was queried by calling engine code. Prior misses using the guessed ABI are not evidence of absence.

## Verified reader correction and checks

The first valid-world run (PID 15255) reached the explicit trigger but could not sample a root pointer with top-byte tag `0xb4`: `SelfMemory` rejected the address against `SSIZE_MAX` before calling the kernel. Evidence: [initial tagged-pointer failure](live-world-registry-explicit-2026-10-16.txt). A test using only its own allocation plus an injected top-byte tag reproduced the failure on M51 (`self memory reader check failed`, exit 1). The ARM64 Android reader now strips only the top-byte metadata before kernel-assisted reads, preserving read failure handling and overflow bounds.

After the correction, the M51 test passed tagged owned allocation reads, tagged inaccessible-address rejection, ordinary reads, inaccessible/null/empty reads, and overflow rejection. This is a reader test, not gameplay proof. The corrected in-game sample above is the separate real-engine validation. A stable build also exposed missing disabled stubs for three existing experimental exports; those stubs were added so the OFF configuration links and keeps those operations unavailable.

## Boundary and next prerequisite

Do not repeat guessed-string lookups. The concrete next contract is the composite key comparison/construction used by the existing `0x103e5cac` path, followed by the producer/insertion path for one of these now-identified vanilla objects. The registration phase/thread, factory arguments, ownership, block-state/runtime-ID assignment and finalization remain unknown. No registry writes, engine hooks, block registration, or gameplay capabilities were enabled. `hybridmcpe` is not registered by this diagnostic.
