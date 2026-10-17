# Cobblestone constructor to registry observation

Target: Minecraft Bedrock 1.26.50.4 ARM64 on Samsung Galaxy M51. Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

## Experiment A: constructor identity

The forward-only hook at ELF VA `0x100fb840` fired once during world loading in game PID `10011`, thread `10051`. It recorded `x0/this=0xb4000077b6d07d10`, `x1=0x763987dfa0`, `x2=4`, and `x3=0xb4000076c74a67b0`; the constructor returned with the same `this` pointer. The hook forwarded all four observed general-purpose arguments and did not dereference the object. Runtime return PC `0x7628456cc0` with load bias `0x761f093000` resolves to ELF VA `0x93c3cc0`, placing the call instruction at `0x93c3cbc`.

After the user confirmed the disposable world HUD, the one-shot read-only probe ran in PID `10011`, thread `10724`. Before world loading, the same registry object `0x763256adb8` had `root=0`. In the live-world sample its root was non-null. The node whose encoded key contains `minecraft:cobblestone` had `value48=0xb4000077b6d07d10`, RTTI `CobblestoneBlock`, and `cobblestone_ctor_match=1`. The probe reported `engine_calls=0;mutation=none`. A post-probe screenshot shows the game still in-world.

This establishes pointer identity from the observed CobblestoneBlock constructor output to the value field of the sampled cobblestone registry node, with a concrete constructor callsite at `0x93c3cbc`. It does not establish ownership transfer or the complete registration ABI.

## Experiment B: states and palette

Not established. No verified BlockState enumeration, BlockPalette/runtime-ID interface, or safe placement-time observation point was available in this test. The runtime diagnostic correctly kept construction/registration capability blocked and reported `states=unavailable;palette=unavailable`; no engine function was called for those operations.

## Crash correction and validation

The earlier experimental proxy forwarded only `x0`, although the observed constructor call site supplied `x0` through `x3`. Its minidump showed a null dereference at engine ELF VA `0x10108468` (`ldr x0,[x0,#0x20]`) in the constructor path. The proxy now forwards all four observed arguments. In the corrected run, the constructor hook fired, registry identity matched, the native self-hook cleanup test passed, and PID `10011` remained in the disposable world after the probe. This supports the dropped-arguments diagnosis; it does not prove every constructor call site uses the same full contract.

## Artifacts

- Filtered device log: `constructor-registry-probe-2026-10-17.txt`
- HUD before probe: `constructor-probe-world-hud-2026-10-17.png`
- HUD after probe: `constructor-probe-world-after-2026-10-17.png`

ARM64 native libraries and the signed debug APK rebuilt successfully after correcting the installer ABI label. That label-only source correction was not installed during this capture; the captured runtime already used four-register forwarding.
