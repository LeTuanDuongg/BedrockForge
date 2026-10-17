# Repeated self-hook fix: isolated M51 ARM64 regression

The shared-mode self-test proxy did not pop ShadowHook's per-thread proxy stack.
`sh_hub.c` uses that stack to skip recursive proxy calls with the same original
trampoline address. `sh_enter.c` retires entry trampolines for 15 seconds; a
later hook can reuse that address. The stale frame then made the proxy get
skipped, leaving the original return value 42 with zero proxy/original counters.

The regression executable performs three install/call/remove checks, waiting
16 seconds between them. On M51, the uncorrected build passed run 1 and failed
run 2 at `behavior_check`. The corrected proxy uses `SHADOWHOOK_STACK_SCOPE()`
and `SHADOWHOOK_CALL_PREV`, as the working Button Duration proxy already does.
Failure paths now explicitly log unhook status and retained handles.

The same delayed test passed all three runs after the fix: one proxy hit, one
original call, return 42, verified cleanup each time, exit 0.

- Before: [self-hook-lifecycle-before-2026-10-17.txt](self-hook-lifecycle-before-2026-10-17.txt).
- After: [self-hook-lifecycle-after-2026-10-17.txt](self-hook-lifecycle-after-2026-10-17.txt).
- NDK: `28.2.13676358`, `arm64-v8a`, `RelWithDebInfo`, experimental native ON.
- Built `libhybridmcpe.so` SHA-256: `348f430bd3255bbe55fad22021065d22b63204720af6d961b52cd8b997399494`.
- Test executable SHA-256: `879080c8fd763766b01225b49805252a0fcc60c9c7960d3062c19fe6f0ef9aba`.
- Location: isolated `/data/local/tmp/hybridmcpe-hook-lifecycle/` on Samsung M51.

This is a real-device isolated hooking regression, **not** Minecraft gameplay
validation. The corrected library was not installed into the running launcher.
The existing working APK, Button Duration implementation, public Mod ABI 1,
installed Minecraft and worlds were not changed by this test. It does not
explain the older unrelated engine-trace SIGSEGV.
