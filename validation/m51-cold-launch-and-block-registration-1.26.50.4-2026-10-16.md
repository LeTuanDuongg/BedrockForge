# M51 cold-launch validation — Minecraft 1.26.50.4

## Result

Three consecutive cold starts of the HybridMCPE host reached the Minecraft menu and opened the disposable `My World` test world on a Samsung Galaxy M51 (SM-M515F, Android 12, ARM64). All three game sessions loaded the HybridMCPE diagnostic module in the same process as the engine. No Minecraft crash or unexpected process exit was observed during these runs.

This verifies host/runtime stability for these three runs. It does **not** verify gameplay integration: the diagnostic reports `capabilities=0`, `layout_validated=0`, and `gameplay_binding=unavailable`.

## Target identity

- Minecraft version: `1.26.50.4`
- Engine ELF SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`
- Engine profile: `bedrock-1.26.50.4-arm64`
- Host APK SHA-256: `18eeae5b292667fe382f2c48fc0d9fa739cda1ad079cbf0a5f6e994ed8473a5c`
- Disposable world: `My World`; saved after the test runs. Original Minecraft installation, accounts, and other worlds were not changed.

## Runs

| Run | Game PID | Menu | Test world | Diagnostic evidence |
|---|---:|---|---|---|
| 1 | 14731 | Reached | Opened and saved | `diagnostic_mod=start abi=1 pid=14731 capabilities=0`; engine hash/profile matched target |
| 2 | 16066 | Reached | Opened and saved | `diagnostic_mod=start abi=1 pid=16066 capabilities=0`; engine hash/profile matched target |
| 3 | 17753 | Reached | Opened | Same-PID diagnostic start; Android Recents/background and return produced `diagnostic_suspend=0`, then `diagnostic_resume=0`; PID remained 17753 |

Screenshots are in ignored `build/evidence/` as `cold-1-after90s.png`, `cold-1-world-after80s.png`, `cold-1-saved-world-list.png`, `cold-2-after90s.png`, `cold-2-world-rendered.png`, `cold-2-saved-final.png`, `cold-3-world.png`, `cold-3-recents.png`, and `cold-3-resumed.png`. Full filtered logs were retained for runs 1 and 3 as `cold-1-full.log` and `cold-3-full.log`; run 2's focused device log output was observed during the test but not saved as a separate full log artifact.

## Block-registration gate

The exact 1.26.50.4 ELF contains block-registry and definition-related diagnostic strings. These establish that registry and definition systems exist, but string presence does not identify a callable registration contract.

A separate Ghidra 12.1.4 project was created for this exact ELF, leaving all 1.26.52.3 projects untouched. Headless full analysis was stopped after more than eight minutes: repeated `LSDACallSiteTable` and `GccExceptionAnalyzer` errors continued, and the targeted xref/decompilation report was never produced. Therefore there is no verified 1.26.50.4 registration call chain or ABI from this attempt.

Still unknown: registry mutation entry point and calling convention; identifier/type and definition construction; factory requirements; ownership/destruction; registration/finalization phase; and thread requirements. No engine address was invoked, no gameplay capability was enabled, and `hybridmcpe` was not registered in Minecraft.

**Next gate:** obtain complete, version-pinned caller/disassembly evidence for registry initialization through one vanilla block registration and finalization, or equivalent supported runtime evidence. Only then define and test a bridge contract. Until that gate passes, native block registration remains unavailable.
