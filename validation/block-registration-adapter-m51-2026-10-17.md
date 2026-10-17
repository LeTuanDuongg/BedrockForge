# Native block registration adapter — M51 validation

- Device: Samsung SM-M515F (M51), Android 12; ADB reported `RF8R31WEK5A device`.
- Target: Minecraft Bedrock 1.26.50.4 ARM64; engine SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Installed HybridMCPE APK SHA-256 matched the built APK: `332ABB5F80D341660920AB595660AB49D680E52E8E95BC8AB11171997CA93110`.
- HybridMCPE game process: PID `31452`; diagnostic Mod ABI 1 started with `capabilities=0`.
- The actual Minecraft main menu was captured at [m51-block-reg-probe-31452.png](m51-block-reg-probe-31452.png). The disposable `HybirdMCPE_check` world HUD was captured at [m51-block-reg-world-31452.png](m51-block-reg-world-31452.png).
- Menu probe: registry identity `0x762ed23db8`, root `0x0`.
- World probe ran on TID `32599`; same registry identity, root `0xb4000077068d4cb0`. Bounded sample included `minecraft:cobblestone` with RTTI `CobblestoneBlock`, and `minecraft:bedrock` with RTTI `BedrockBlock`.
- Adapter result: `block_registration=blocked;reason=constructor_and_state_contract_unverified`; `engine_calls=0;memory_writes=0`.
- ARM64 self-hook diagnostic passed `1/1`; original return `42`, cleanup verified. Host remained in the test world after the read-only probe.
- No custom block was registered. Runtime state IDs, acquisition, placement and save/reload remain untested.

## Next experiment

Use a bounded hardware watchpoint or a validated observational hook on the live Cobblestone object/value path, starting before the relevant world initialization phase. Capture its downstream state/palette consumer and ownership transitions. Keep registration disabled until that engine contract is verified.
