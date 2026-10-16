# Native Engine Bridge milestone

Target: Minecraft Bedrock Android 1.26.52.3 ARM64, engine SHA-256 `53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a`.

The existing Ghidra project was copied into ignored `build/ghidra-research`. Targeted scripts inspected the already analyzed program and seeded existing function boundaries from the target's RTTI-backed vtables. They used `-noanalysis`; no full Auto Analysis was run. The copied database identifies the image as AArch64 and uses an artificial `0x100000` image base. The candidate addresses in the contract JSON are ELF virtual addresses, not Ghidra addresses or runtime pointers.

The machine-level evidence does not yet establish a callable engine API. The copied Ghidra database reports unknown calling convention and zero parameters for all candidates, even where the decompiler invents C-like parameter lists. Some methods have no code xrefs because their observed references are vtable data, not direct calls. Large functions contain PIC and control-flow warnings. RTTI names and vtable entries identify classes and candidate virtual slots; they do not establish ownership, semantics or safe call contracts. Details and per-candidate evidence are in [the target contract record](../targets/bedrock-1.26.52.3-arm64-engine-contracts.json).

The strongest narrow observations are:

- `ContainerManagerModel` slot 2 loads a byte at `this+0x50`; slot 3 stores one there. Slot 4 independently loads `this+0x51`. This supports adjacent byte-sized state, but not a slot-count interpretation.
- `ItemStack` RTTI/vtable is now confirmed from the engine's RELA-backed metadata. Slot 0 has 868 direct call references. Sample callsites put stack/local addresses into `x0`; the entry rewrites the object's vtable pointer and processes state at `+0x80` and `+0x90`. This is strong evidence for a destructor/cleanup routine and stack-value lifetime, but it does not define inventory ownership, copy/move behavior or a callable C++ ABI.
- `ChestBlockActor` slot 0 rewrites its primary and subobject vtable pointers and traverses member state. Slot 1 calls slot 0 and tail-branches to an unresolved address. This supports a teardown/deallocation path, not construction, storage ownership or serialization.
- An `Inventory` slot tail-dispatches through vtable byte offset `0x188` with constant `0x24`; the destination and operation remain unknown. `InventoryTransactionPacket` slots expose constants and small metadata-like returns, not a verified transaction path.

Ghidra records no references to these vtable address points, and several existing function bodies span implausibly large ranges. Direct call references are useful where present, but their caller boundaries and decompilations remain incomplete. No candidate is suitable for implementation or invocation. Additional evidence is recorded in the target contract JSON.

The private native Engine Bridge now owns engine fingerprint and five-layout validation. It publishes zero gameplay capabilities after successful identity validation. The public Mod ABI remains unchanged; `HYBRID_CAP_CONTAINER`, `HYBRID_CAP_CHEST_UI` and `HYBRID_CAP_PERSISTENCE` remain unavailable, and mods requiring them fail closed. No engine method is called.

## Iron Chest dependency map

| Requirement | Evidence state | Consequence |
|---|---|---|
| Correct engine identity and five guarded layouts | Verified on prior M51 bootstrap milestone | Safe to identify the target image; grants no gameplay access |
| Register a placeable custom chest block | Unknown | Cannot place an Iron Chest |
| Construct/destroy a block actor with a 54-slot inventory | Unknown | No real chest storage or ownership contract |
| Validate item moves and container transactions | Unknown | No safe transfer path |
| Create/navigate a vanilla-style chest screen | Unknown | No functional chest GUI |
| Route native touchscreen interactions | Unknown | Touch behavior unverified |
| Serialize and restore contents through world storage | Unknown | Persistence unverified |

The M51 milestone established bootstrap execution, profile/layout validation and Mod ABI start/stop in a game process. It did not establish a stable modified APK or interactive-world behavior. The modified-APK deployment route remains NO-GO; this research did not alter or reinstall any Minecraft package or touch user worlds.

## Native test validation

The earlier ADB failure was sandbox socket denial (`127.0.0.1:5037`, Windows error 10013), not a device authorization failure or a competing daemon. Starting the Android SDK's ADB server outside the sandbox restored connectivity; the M51 appeared as `device` (authorized). NDK 28.2.13676358 / Clang 19.0.1 built the ARM64 tests, and both existing native test executables passed on the M51 in an isolated shell process. Exact binary hashes and results are in [the device validation record](../validation/m51-native-tests-2026-10-16.json). The tests were not run inside Minecraft.

## Minimum safe integration-test prerequisites

Native gameplay experiments need an authorized startup host that loads HybridMCPE inside the game process without replacing the original installation or bypassing integrity/licensing. Before any engine call, that process must report the expected engine SHA, ARM64 ABI and profile/layout validation, and a disposable copy of a world must be available with user data kept separate. Logs must capture the process ID, module map, capability set and lifecycle result. The modified-APK route currently fails these prerequisites because its package does not stay running; it remains NO-GO.

Once a permitted host exists, the first runtime experiment should only verify library mapping, profile matching, Mod ABI start/stop and fail-closed capability reporting. Constructing or mutating native ItemStacks, processing real container transactions, opening a chest screen, or testing persistence all require that working host. World tests additionally require an expendable test world. No such experiment was run here.

## Authorized deployment status

The [first custom-block integration audit](engine-integration-test-block.md) records the current source-based process boundary, M51 package identity, corrected Preloader unload behavior and unwind-bounded block-registry observations. It does not establish block registration or gameplay. The original target, zero gameplay capabilities and modified-APK NO-GO result remain unchanged.

Microsoft's current Bedrock creator documentation describes Android delivery through Add-On/world-pack import and the JavaScript Script API. I found no documented, supported native C++ in-process mod-loading interface for Bedrock Android in those official references ([Add-On install guidance](https://learn.microsoft.com/en-us/minecraft/creator/documents/gettingstarted?view=minecraft-bedrock-stable), [Script API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi)). This does not prove no private partner interface exists; it means HybridMCPE has no verified authorized native host to target today. The existing native runtime and Mod ABI remain usable if such a host or a permitted local test build becomes available.
