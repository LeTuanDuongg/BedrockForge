# Registry lifecycle and self-hook check — 2026-10-17

Target: Bedrock 1.26.50.4 ARM64 on Samsung SM-M515F (M51). Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

## Live registry observation

The explicit, one-shot read-only probe ran after the Minecraft HUD was visible in the disposable `HybridMCPE_check` world. The before-probe HUD capture is `hybridmcpe-current.png`; the after-probe capture is `hybridmcpe-registration-world-2026-10-17.png`.

At 13:07:21.615 (+07:00), process 3864 received phase `world_loaded_manual`; worker thread 7364 sampled the engine at 13:07:26.386. It reported the same registry address previously seen in this process at native Mod ABI start and main menu (`0x762d72cdb8`). The root was null at the earlier samples and non-null in the world (`0xb4000077068eef30`). The bounded sample reported RTTI names `UnknownBlock`, `CobblestoneBlock`, and `BedrockBlock`; a fourth key's bytes contain `minecraft:stone_button`, but its value was not validated. No engine function was called and no memory was changed.

This bounds population of the observed lookup tree to after the menu sample and by the loaded-world sample. It does not identify the exact registration callback or prove the first insertion time; snapshots were asynchronous and unlocked. The world remained in the game process after the probe, though the player had died and the death screen was visible.

The nearest existing static lead remains the definition validation/duplicate-ID path at ELF VA `0x10487d40`, whose known caller validates existing identifiers and dispatches on found definitions. Evidence does not connect it to allocation or registry insertion. No custom block registration was attempted.

## Repeated self-hook observation

At 13:04:26.385, the second self-test run in process 3864 saw the original four target instructions, installed a branch, and then called the target. It returned 42 but logged `proxy_hits=0` and `original_calls=0`, so the behavior check failed. This is a reproducible diagnostic failure after a prior passing run, not evidence that the separate Minecraft engine hook failed. The error branch attempted unhooking but did not log the unhook result or verify restored bytes; cleanup for this failed run remains unknown. The Minecraft process stayed alive.

## Build and scope

The experimental Android ARM64 APK built successfully and was installed over the HybridMCPE test package. APK SHA-256: `4cf23a68f85feb5b1cc92050c72686a5c992a3d6bd30d72d66d01173fceef33c`. The original Minecraft package and worlds were not modified. The live test validates the diagnostic trigger and registry snapshot only; it does not validate a callable registration ABI or gameplay capability.
