# Passive call-trace crash isolation — 2026-10-16

## Crash evidence

- Target: Minecraft Bedrock 1.26.50.4 ARM64, engine SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Device: Samsung SM-M515F (Galaxy M51), Android 12.
- Crashed game process: `dev.hybridmcpe.launcher:game`, PID `12201`.
- Android exit result: `SIGNALED`, status `11` (`SIGSEGV`), at `2026-10-16 20:07:33.267`.
- Bedrock logged a Breakpad dump at `.../data/crash/1d80789c-84e6-4b4a-86e0-87c4176b6dcc.dmp`.
- Captured log has no `candidate_trace=hit`, and does not show a completed installation event. Therefore it does not establish whether the patch was installed or reached.
- Android supplied no exit trace. The host is not debuggable; `run-as` cannot access its private dump. No isolation bypass was attempted.

## Confirmed code-path defect

Before this change, `HostedMinecraftActivity.onCreate()` called `startExperimentalCandidateTrace()` immediately after the imported Activity's `onCreate()` returned. Later, loading `hybridmcpe:experimental-registry-probe` called the same start function again from Mod ABI `start()`. The second call was intended to be idempotent, but the first call could attempt patching before the mod-load stage and engine lifecycle were recorded. Existing logs do not prove that either call reached or completed the patch.

This establishes an unsafe duplicate/early installation path, not the precise SIGSEGV cause. The failing trace build also replaced executable engine text using `mprotect(PROT_READ|PROT_WRITE|PROT_EXEC)`, installed a process-wide `SIGTRAP` handler, and redirected the signal context. No evidence currently distinguishes those risks or the startup timing as the actual fault source.

## Isolation change

- Removed all instruction patching, signal handling and hook redirection from the currently built trace implementation.
- Removed the early JNI trace invocation from `HostedMinecraftActivity.onCreate()`; that stage now only records that diagnostics are armed and defers engine inspection until Mod ABI `start()`.
- The passive probe records monotonic timestamps, PID and TID at module start, engine verification start/end, engine mapping resolution, candidate validation and skipped hook installation.
- Candidate validation checks the exact profile, executable PT_LOAD ranges, readable callsite, expected `BL` instruction and decoded target. It reads no call arguments and makes no engine call.
- `HYBRID_EXPERIMENTAL_INTERCEPTION` defaults OFF; the build script forces OFF and CMake rejects enabling it while this crash remains unresolved.
- Mod ABI 1 and the existing registry getter are unchanged.

## Build and device status

- ARM64 build command: `tools/build.ps1 -PreloaderAdapter -ExperimentalNative`.
- Build completed successfully with Android NDK 28.2.13676358.
- Final passive APK SHA-256: `21A8BAAF6D56E32D83916B215B1612CD2D18DCAEAF2AA58CD81353AED41C3864`.
- Prior working experimental APK preserved at `dist/HybridMCPE-experimental-native-1.26.50.4.apk`, SHA-256 `CB215097E6178AD8F751B6AFF748C715C3076BC132CD7080030E0C5567571C91`.
- The passive APK was installed as an update to `dev.hybridmcpe.launcher`; the original Minecraft package and user data were not modified.
- The launcher showed the imported 1.26.50.4 profile and exact engine hash before launch.
- PID `17996` completed all passive checks. Android later reported its exit reason as `USER REQUESTED`, description `stop ... due to installPackageLI`; a second update was being installed. This was not a native crash.
- PID `19186` then completed the same checks and remained alive as `dev.hybridmcpe.launcher:game`. The foreground activity was `HostedMinecraftActivity`; the captured game screen showed the real Minecraft 1.26.50.4 main menu.
- In PID `19186`, Android logs show: host Activity returned at `20:21:30.256`; native mod load began at `20:21:37.961`; Mod ABI start entered at `20:21:37.962`; registry getter returned at `20:21:42.036`; engine verification completed at `20:21:46.489`; target mapping and candidate validation completed at `20:21:46.489`; hook installation was explicitly skipped with `code_mutation=none`; module load returned `0`.
- Exact candidate checks passed in that process: engine base `0x761782f000`, callsite `0x762736b1ac` (RVA `0xfb3c1ac`), target `0x7627cb6d40` (RVA `0x10487d40`), instruction `0x94252ee5`.
- No `candidate_trace=hit` event is expected or claimed because interception remained off. No world was opened.
- Timestamped focused logs: `build/evidence/passive-trace-1.26.50.4-m51.log`. Main-menu capture: `build/evidence/passive-trace-current.png`.

## Next validation

The known-working registry-probe APK remains the A baseline. B and C passed in the real game process with interception disabled. Do not re-enable interception yet: the old crash still lacks a faulting PC, signal code and native backtrace, so the specific unsafe operation remains unknown.

No hook hit, engine function interception, block registration or gameplay change is claimed.
