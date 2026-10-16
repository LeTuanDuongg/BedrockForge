# Native button-duration experiment — M51

## Result

An experimental in-process hook extended one vanilla stone-button pulse in Minecraft 1.26.50.4 from its observed 30-tick schedule to 200 ticks. The connected redstone lamp was bright at 2 seconds with the change enabled; the same setup was dark by 2 seconds with the original schedule. It returned to dark near the 10-second mark during the modified run. The hook was then disabled and removed; a subsequent press again returned to dark by 2 seconds.

This is a real gameplay change in the disposable `My World` running through HybridMCPE. It is not block registration, a stable SDK capability, or a production-ready engine API.

## Device and build

- Device: Samsung Galaxy M51 / SM-M515F, Android 12, ARM64.
- Game process during the modified run: PID 13491.
- Target profile: `bedrock-1.26.50.4-arm64`.
- Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Tested APK: `dist/HybridMCPE-debug.apk`, SHA-256 `C8A11D6C8C29D38A355AFEBB17ABC17C8AB34C9CF56BA571170898410C939801`.
- HybridMCPE diagnostic Mod ABI 1 loaded in PID 13491. Existing gameplay capability mask remained zero.

## Runtime evidence

The observer was installed only after the exact engine hash and executable bytes were checked. It hooked the scheduler at runtime address `0x762cf97dc8` (ELF offset `0x0fb8edc8`). The observed call returned from the verified ButtonBlock press callsite at runtime address `0x762d51dc44` (return RVA `0x10114c44`).

On the Minecraft gameplay thread TID 14123 (`Thread-19`), a real press produced:

```text
button_hook=hit;pid=13491;tid=14123;caller_rva=0x10114c44;state=0xb400007776f443b0;block=0xb4000077b6e9f9e0;vtable=0x76304e4f68;pos=-1,94,22;before=30;after=30;flags=0,0;hit=1
button_hook=original_returned;pid=13491;tid=14123
```

The runtime vtable equals the exact-hash profile's `ButtonBlock` vtable address. The original scheduler returned normally. After explicit enable, the same call showed `before=30;after=200`; the original scheduler again returned normally. Only this return address, exact ButtonBlock vtable and the observed 20/30-tick values qualify for the change. Other scheduler calls and all other block classes are forwarded unchanged.

The modification is reversible. The reset command first disables the replacement and then unhooks; device log recorded `button_hook=cleanup;result=0;pid=13491;hits=2;retained=0`. No engine binary bytes were changed. The game process remained alive, and no native crash was recorded in the experiment logs.

## Visual evidence

All images below are from the same live disposable world and camera position.

| State | Capture |
|---|---|
| Before press: lamp dark | [before-dark.png](native-button-modification-1.26.50.4-m51/before-dark.png) |
| Modified press, about 0.35 s: lamp lit | [after-0.35s-lit.png](native-button-modification-1.26.50.4-m51/after-0.35s-lit.png) |
| Modified press, about 2 s: lamp still lit | [after-2s-lit.png](native-button-modification-1.26.50.4-m51/after-2s-lit.png) |
| Modified press, about 10 s: lamp dark | [after-10s-off.png](native-button-modification-1.26.50.4-m51/after-10s-off.png) |
| After reset and vanilla press, about 2 s: lamp dark | [restored-2s-off.png](native-button-modification-1.26.50.4-m51/restored-2s-off.png) |

## Limits

The AArch64 callsite and runtime behavior establish the six-register scheduler forwarding needed for this one observed path. The semantic names of the scheduler's remaining flags, broader object lifetime guarantees, other button states, and behavior on other engine builds remain unverified. The experiment stays in the experimental module behind the exact fingerprint gate; it is not exposed through Mod ABI 1 or enabled in stable mode.
