# Button Duration Mod ABI 1 validation — M51

Target engine: Minecraft 1.26.50.4 ARM64, SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

Test APK SHA-256: `8544AD67DA8F9D51464E0B7249217D06D8172886D8AE2E4CA803ADF163C9B835`.

Post-validation rebuild after making the disabled state hook-free: `FAAF2288AC5557C87B923E3AF2EA56D8AF429A456873EA68DDB8992D0609C4E5` (ARM64 build passed; installed APK hash on M51 matches).

Filtered device logs: [button-duration-mod-abi1-m51.log](button-duration-mod-abi1-m51.log).

Device: Samsung Galaxy M51 (`SM_M515F`, Android 12). Game process: PID `9054` (`dev.hybridmcpe.launcher:game`). The independent module `hybridmcpe:button-duration-experiment` loaded through Mod ABI 1; start returned `0` and the bridge installed in observe-only mode.

Observed sequence in the disposable `My World`:

- Vanilla: scheduler hit logged `before=30;after=30`. At 2 seconds the lamp was dark: [baseline screenshot](button-duration-mod-m51-before-2s.png).
- Enabled: control returned `0`; the same engine call logged `before=30;after=200`. At 2 seconds the lamp remained lit: [enabled screenshot](button-duration-mod-m51-enabled-2s.png).
- Disabled: control returned `0`; Shadowhook cleanup returned `0;retained=0`. At 2 seconds the lamp was dark again: [restored screenshot](button-duration-mod-m51-restored-2s.png).
- Repeated enable/disable/enable returned `0` each time. The re-enabled hook recorded another `30→200` hit. The game remained in PID `9054` during this sequence.
- Android background/foreground lifecycle: pause called mod stop, unhook returned `0`, and `diagnostic_suspend=0`. Resume reinstalled the hook, restored `enabled=1`, and returned `diagnostic_resume=0`, still in PID `9054`.

The game saved and returned to its world list ([capture](button-duration-mod-m51-reentry.png)). A later re-entry attempt selected `My World` and reached the Minecraft loading screen, but the game process then exited. Android recorded PID `17144` exiting with signal 11 (SIGSEGV) at `10:56:18`; the screen returned to HybridMCPE ([capture](button-duration-mod-m51-reentry-state.png)). Bedrock Breakpad reported a dump under the app-private directory, which was not accessed. Available logs do not identify the faulting instruction or prove that the button hook caused the crash. World re-entry therefore failed and remains unsafe/unverified for this build. No personal world or original Minecraft installation was used or modified.

The executable proof covers a real ButtonBlock callback, behavior change, visual result, unhook restoration, repeated toggles and Activity pause/resume. It does not establish safety for arbitrary engine hooks or world-specific object lifetimes.

## Hook-free start follow-up

The hook-free disabled-start change was built and installed on 2026-10-17. At 11:33:14, PID `11710` logged `button_mod=lifecycle_start;enabled=0` and `button_mod_load=0`; the engine profile hash matched. Minecraft reached its menu, then entered the newly created `HybridMCPE_check` test world with its HUD visible in the same PID. Android Back opened Pause; Save & Quit returned to the world list, which showed the new third world. The two earlier worlds were left unopened. A hook-free re-entry also reached the HUD in PID `11710`.

In the same world, explicit enable/disable commands returned `0`. Before the first enabled re-entry attempt, PID `11710` exited at `11:48:43.800`; Android `ApplicationExitInfo` reported `reason=SIGNALED`, `status=11`. Available crash-buffer output does not identify a faulting PC or establish whether the hook caused the exit.

The game restarted as PID `18117` with the same engine SHA-256. The button mod loaded disabled. It was explicitly enabled at `11:52:28.580` (`operation=1;result=0;enabled=1`). The user then saved and quit `HybridMCPE_check`, re-entered it, and confirmed the HUD. The captured screen shows the live gameplay HUD ([enabled-hook re-entry screenshot](button-duration-mod-m51-enabled-reentry.png)); the process remained PID `18117`. After re-entry, reset, enable, reset each returned `0` at `11:53:47.513`, `11:53:52.783`, and `11:53:53.197`; PID `18117` remained alive and the final state was disabled. The user-reported world transition plus screenshot prove re-entry with the hook enabled in the same process, but do not explain PID `11710`'s earlier signal-11 exit.

Exact runtime excerpts and the captured exit metadata are recorded in [re-entry evidence](button-duration-mod-reentry-m51-2026-10-17.txt). These results cover a real enabled-hook world re-entry and successful repeated toggles. The earlier signal-11 termination remains unexplained; no crash PC/backtrace was available. The test used only disposable `HybridMCPE_check`; original Minecraft data and other worlds were not touched.
