# Native Engine Bridge milestone

Target: Minecraft Bedrock Android 1.26.52.3 ARM64, engine SHA-256 `53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a`.

The existing Ghidra project was copied into ignored `build/ghidra-research`. Targeted scripts inspected the already analyzed program and seeded existing function boundaries from the target's RTTI-backed vtables. They used `-noanalysis`; no full Auto Analysis was run. The copied database identifies the image as AArch64 and uses an artificial `0x100000` image base. The candidate addresses in the contract JSON are ELF virtual addresses, not Ghidra addresses or runtime pointers.

The machine-level evidence does not yet establish a callable engine API. The copied Ghidra database reports unknown calling convention and zero parameters for all candidates, even where the decompiler invents C-like parameter lists. Some methods have no code xrefs because their observed references are vtable data, not direct calls. Large functions contain PIC and control-flow warnings. RTTI names and vtable entries identify classes and candidate virtual slots; they do not establish ownership, semantics or safe call contracts. Details and per-candidate evidence are in [the target contract record](../targets/bedrock-1.26.52.3-arm64-engine-contracts.json).

The strongest narrow observation is that two `ContainerManagerModel` slots load and store a byte at object offset `0x50`. Its meaning is unknown. An `Inventory` slot tail-dispatches through vtable byte offset `0x188` with constant `0x24`; the destination and operation are unknown. Chest actor and chest screen candidates are complex and unresolved. `InventoryTransactionPacket` candidates expose constants, not a verified transaction path. No candidate is suitable for implementation or invocation.

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

## Authorized deployment status

Microsoft's current Bedrock creator documentation describes Android delivery through Add-On/world-pack import and the JavaScript Script API. I found no documented, supported native C++ in-process mod-loading interface for Bedrock Android in those official references ([Add-On install guidance](https://learn.microsoft.com/en-us/minecraft/creator/documents/gettingstarted?view=minecraft-bedrock-stable), [Script API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi)). This does not prove no private partner interface exists; it means HybridMCPE has no verified authorized native host to target today. The existing native runtime and Mod ABI remain usable if such a host or a permitted local test build becomes available.
