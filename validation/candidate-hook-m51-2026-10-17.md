# Candidate hook observation on M51 — 2026-10-17

## Result

The observational hook on the 1.26.50.4 candidate at ELF RVA `0x10487d40` installed and intercepted one real call while the disposable `HybridMCPE_check` world was opening. The original function returned normally. Minecraft then reached its gameplay HUD and remained in PID `23290` during the check.

This confirms runtime reachability during world loading only. It does not establish that this function inserts a block, nor does it confirm a registration callback or a playable custom block.

## Exact target and tested build

- Device: Samsung Galaxy M51 (`SM-M515F`), Android 12, ARM64.
- Game process: `dev.hybridmcpe.launcher:game`, PID `23290`.
- Engine profile: `bedrock-1.26.50.4-arm64`.
- Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Tested HybridMCPE APK SHA-256: `85a626f79396df9537310cb34b93176978d971720f5b8b773f846905796bbe2a`.
- Hook target ELF virtual address: `0x10487d40`; callsite ELF virtual address: `0xfb3c1ac`. Runtime addresses are `load_bias + ELF_VA` for this ET_DYN image; they are not file offsets.
- Runtime target address logged by the hook: `0x762a6b6d40`.
- Gameplay HUD evidence: [hybridmcpe-world-hud.png](hybridmcpe-world-hud.png).

## Runtime timeline

- `12:02:56.421`: registry read-only probe completed in PID `23290`; no mutation.
- `12:03:01.234`: target and callsite resolved in the mapped engine image.
- `12:03:06.012`: hook installed after exact profile/hash and executable-range/instruction checks.
- `12:05:19.491`: Minecraft logged opening the isolated profile's world database.
- `12:05:19.658`: hook observed hit 1 on thread `23336` (`PID 23290`). It logged raw arguments only: `x0=0xb400007766a5fb70`, `x1=0xb400007786bd4290`, `x2=0xb4000077c681d2c8`, `x3=0x76111291e0`.
- `12:05:19.689`: original returned raw `x0=0x2`; the meaning of this value is unknown.
- `12:05:23.685`: Minecraft logged player connection.
- Subsequent screen capture showed the in-world HUD; PID `23290` remained alive.

## Follow-up diagnostic build

The follow-up build added bounded entry logging for raw `x0`–`x3`, Shadowhook's captured return PC, the expected return PC (`callsite + 4`), a comparison-only registry-address match mask, and monotonic time. It does not dereference arguments or change the original call. APK SHA-256: `67edc0b9c106ef6d39eec54d7644e72ae121a5dc6e6a3e09b07e21bcd2cdc29e`.

On M51 PID `27298`, the engine fingerprint and mapping checks passed and the hook installed at runtime address `0x762c2d0d40`; the callsite was `0x762b9851ac`, expected return PC `0x762b9851b0`, and load bias `0x761be49000`. The registry global address logged was `0x762f320db8`, matching the prior verified getter result. Initial runs were paused/stopped when Android's 60-second display timeout expired; module stop removed the hook cleanly (`result=0`, `retained=0`). The device timeout was temporarily set to ten minutes for the authorized isolated test.

On the next entry to `HybridMCPE_check`, the hook recorded hit 1 at `12:28:36.747`, PID `27298`, TID `27340`. Raw args were `x0=0xb400007766a3a4f0`, `x1=0xb400007786bea790`, `x2=0xb4000077c68767e8`, `x3=0x763a5f61e0`. Captured return PC `0x762b9851b0` exactly matched expected `callsite + 4`; registry-address match mask was `0x0`. No argument was dereferenced. The original returned `0x2` at `12:28:36.788`; Minecraft logged player connected at `12:28:40.859`. A later screen capture showed the real gameplay HUD in the same PID: [candidate-hook-followup-screen.png](candidate-hook-followup-screen.png).

The hit occurred during the world-loading transition, before the player-connection log. It must not be described as a confirmed gameplay-action callback.

## Interpretation and limits

Static evidence for this exact ELF shows the candidate performs block-identifier duplicate checks and data-driven definition parsing/validation. Its known caller passes four general-purpose register values, but their C++ types and ownership are not established. The runtime proxy therefore preserved and forwarded the raw register values without dereferencing them or changing the return value.

At caller ELF VA `0xfb3c19c`–`0xfb3c1ac`, the observed setup is `x3=sp`, `x0=x21`, `x1=x22`, `x2=x20`, then `bl 0x10487d40`. The call instruction is `0x94252ee5`. Caller control flow does not consume the returned `x0`; the value `2` therefore cannot be assigned meaning from this call alone. Candidate-side lookup/duplicate-diagnostic evidence supports a definition validation/loading role, but no instruction chain has yet been shown to mutate the verified registry's tree.

The intercepted call is a real engine invocation from the expected caller during world re-entry/loading. Return-PC matching validates that specific call edge. A zero registry-address match only means none of the four raw arguments equals the logged global address; it does not rule out indirect object relationships. Neither the function's full semantic contract nor its relationship to registry insertion is verified. The return value `2` is not interpreted. No new block was created, registered, obtained, placed, or saved.

## Next blocker

Before any registration attempt, evidence must connect this candidate's input objects to a newly constructed definition and identify the actual write that links that definition into the live registry. Ownership, state/runtime-ID creation, finalization timing, and safe registration thread remain unknown. Keep native registration disabled until those links are demonstrated; do not call or repurpose this candidate as a registration API.
