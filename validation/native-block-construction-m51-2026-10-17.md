# Native BlockType construction and default-state experiment

Base commit: `f3fa1f4`. Target: Minecraft 1.26.50.4 ARM64, Samsung M51 / Android 12. Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

## Result

**A and C demonstrated for a temporary, unregistered engine object. B, D, E and F are not achieved.** The object was intentionally destroyed after its default state was validated. There is no new obtainable/placeable block and no custom-block save data.

| Milestone | Actual result |
|---|---|
| A: new BlockType instance | Engine CobblestoneBlock constructor created a distinct instance with canonical name `minecraft:hybridmcpe`; exact engine vtable and captured material matched |
| B: registry recognition | Not attempted; no registry insertion/adoption call was made |
| C: default Block state | Original engine population produced a primary with exact Block vtable and owner pointing to the new type |
| D: palette integration | Not attempted for the new object |
| E: obtain/place | Not available |
| F: custom-block persistence | Not tested; object was never registered or placed |

## Preparation contract narrowed

The ordinary callsite allocates **0x3a8 bytes** at `0x93c3ca0`, through `0xe49677c` (tail branch to engine operator new). It calls CobblestoneBlock constructor `0x100fb840` with x0=allocation, x1=engine string, w2=4 and x3=material pointer.

The base constructor creates the owned component at `+0x388` and its control object at `+0x390`. The 8-byte component wrapper is initialized by `0x1063e794` with a pointer to the type's own `+0x368`. No component is copied from the vanilla object.

A separate operation `0x1010bb6c`, called at `0x93c3cf0`, moves a two-pointer reference-counted attachment into `+0x398/+0x3a0`, clears the source pair and releases the old destination control. Live snapshots prove the distinction:

- Constructor return: canonical vanilla name present, material preserved, `numeric14e=4`, `bits1a4=0`, component pair non-null, attachment pair null, primary null.
- After attachment: source cleared, destination matches source, component pair unchanged.
- Before population: those prepared values remain unchanged; primary is still null.

The engine identifier parser `0x10107be8` handles the no-colon branch at `0x10107d68` using the literal `minecraft:` at ELF `0x269e8a8`. Thus input `hybridmcpe` becomes `minecraft:hybridmcpe`; this is engine normalization, not a manual identifier-field edit.

## Actual C++ experiment

Existing experimental Mod ABI 1 module enables the Engine Bridge hooks. Added a predefined one-shot construction command; it accepts no engine address or arbitrary argument. It is gated by the experimental profile, receiver DUMP permission, exact engine fingerprint, executable mappings and pinned instruction bytes.

On the next natural vanilla Cobblestone constructor thread, the bridge:

1. Uses engine string constructor `0x85380f0` to construct a 24-byte short-string argument for the 10-byte input name. Its verified <=22-byte branch stores length and copies length+1 bytes; the argument needs no heap allocation.
2. Uses engine operator new and the **actual original constructor**, not copied object bytes or a fabricated vtable.
3. Borrows the observed numeric argument **4 only for this unregistered test**, along with the live material pointer. This numeric value is not admitted for registering a new block.
4. At the next verified vanilla population callback, requires the same creation thread and a valid vanilla state. Calls the original population operation on the independent type using that callback's real context.
5. Validates primary Block vtable and owner identity, then calls the engine deleting destructor through the validated type vtable slot `+8`. The ordinary registry replacement path uses this same destructor contract at `0x93c3e60`.

No registry node, palette entry, original block object, Behavior Pack or Script API was used to represent the new type. If construction throws or safe cleanup cannot be established, the bounded allocation is retained and hook shutdown reports failure instead of forcing an unsafe unload. This failure path was not induced on the device.

## M51 evidence

Final PID **12566**, construction/population TID **12609**, at 18:19:44 local time:

- New type: `0xb4000077b6d033c0`, canonical name `minecraft:hybridmcpe`.
- New primary state: `0xb400007777157690`; exact Block vtable and owner validated.
- Population context: `0x1153c21`, consumed only in the observed callback context; its broader semantic contract is not asserted.
- Engine destructor returned; `retained=0`, `registered=0`, `palette_inserted=0`.
- World HUD remained visible; user independently confirmed it. See `native-block-construction-hud-m51-2026-10-17.png`.
- Post-destructor read-only probe at 18:24:51 still found the original Cobblestone registry value `0xb4000077b6d03810`, RTTI `16CobblestoneBlock`, `cobblestone_ctor_match=1`. This is a regression check after the new allocation/destruction, not a custom registration claim.

Exact installed APK was pulled back and matched to the build: SHA-256 `7fc3e7af8dedefb6d282db206607ca6a1792c40639ee1a8465535533483cb177`. Runtime library SHA-256: `b3aefd47493a339813d29fde0b5f422bbab70e85bfd98e49ac86b72ba712bd10`. NDK r28.2 ARM64 build, pinned prologue checks and APK signature verification passed. This exact build includes the corrected **GP3** constructor label.

Three hook shutdown/re-enable cycles passed at the real main menu before world entry: all six control calls returned 0, with **15 successful per-hook removals**, including three removals each of state factory and constructor. The hooks subsequently executed naturally after re-enable during the successful world load. This is hook lifecycle validation, not proof of unloading/reloading the entire Mod ABI library.

After the user saved and quit the test world, a fourth shutdown/re-enable cycle also passed at 18:30:11..18:30:16. A final shutdown left the construction/observation hooks removed. Total: **25 successful per-hook removals**, five successful shutdown controls and four successful re-enable controls, with no pending object preventing cleanup. The final process remains at the world list; construction is disarmed.

Selected original records are in `native-block-construction-m51-2026-10-17.txt`. Known-working pre-construction APK remains in `build/HybridMCPE-type-preparation-before-construction.apk`; device-tested final APK is preserved in `build/HybridMCPE-native-construction-device.apk`. Proprietary binaries are not staged in Git.

## Reproduction in the isolated profile

At the actual menu, send the existing permission-restricted `dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE` broadcast to package `dev.hybridmcpe.launcher`:

- `phase=3`: remove observation hooks; report cleanup failure instead of forcing unload.
- `phase=4`: reinstall exact-version observation hooks.
- `phase=5`: arm construction once for the next vanilla constructor; a duplicate arm returns **-2**. No construction executes on the Android receiver thread.

Then enter only `HybirdMCPE_check`. Default and stable modes do not execute the construction experiment. Public Mod ABI 1 and Button Duration code are unchanged.

## Precise remaining registration contract

**Engine adoption of a new unique identity, with ownership transfer and all associated registry indices, is still unverified.** The ordinary caller also operates on structures at x19 `+0xe0/+0xf8`; their complete relationship to new-type admission is not established. Inserting a tree node and reusing numeric 4 would not establish a valid registration and risks conflicting with Cobblestone.

The next bounded experiment must validate that adoption operation in the already-known ordinary caller around `0x93c3de0..0x93c3e90`, including numeric identity policy and secondary-structure ownership, before handing the now-constructible type into the real population/palette pass. State construction itself is no longer the missing prerequisite. No stable Block API is exposed yet.

Only the successful construction/population/destruction path was exercised. The new population call's exception/crash path and unloading/reloading the entire Mod ABI library were not tested. The experiment is not a production registration interface.
