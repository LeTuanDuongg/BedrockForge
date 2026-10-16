# Experimental native registry probe — 1.26.50.4 ARM64

## Result

The separately selectable Mod ABI 1 experiment loaded in the real Minecraft process and called one narrowly verified, read-only registry getter. It returned the expected engine-owned registry address. No block was registered and no gameplay capability was enabled.

## Exact target and build

- Engine: Minecraft Bedrock 1.26.50.4 ARM64, SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Device: Samsung SM-M515F (Galaxy M51), Android 12.
- Experimental APK: `dist/HybridMCPE-experimental-native-1.26.50.4.apk`, SHA-256 `CB215097E6178AD8F751B6AFF748C715C3076BC132CD7080030E0C5567571C91`.
- Stable-default APK also built with the experiment option off. It packages no experimental module; SHA-256 `ABF4C8780BCA1D4C4A28F7D21BD5F6E5DBC9078B6CA756C0A47A749994F20F25`.
- Only `dev.hybridmcpe.launcher` was updated. `com.mojang.minecraftpe` remained installed; its data was not touched.

## Experiment

Build option `HYBRID_EXPERIMENTAL_NATIVE` adds a separate launcher action and `hybridmcpe:experimental-registry-probe` Mod ABI 1 module. The normal diagnostic path remains unchanged.

The probe requires the exact 1.26.50.4 profile, verifies the loaded ELF hash, executable mapping and getter instruction shape, then calls ELF RVA `0x103e2d54`. The exact ARM64 instructions are `ADRP X0, ...; ADD X0, X0, #0xdb8; RET`. The call has no arguments and returns the embedded registry pointer in `X0`. The returned address must equal RVA `0x134d7db8` and map to writable engine memory. It does not dereference that returned object or mutate engine state.

## M51 evidence

The first probe attempt reported `reason=owner_mismatch`; that was a probe-check bug. It compared the object address with the first word stored inside the object. The check was corrected to compare the getter's returned address to the known mapped object address.

Successful run:

- Game process PID `26436`.
- `250125.328`: diagnostic Mod ABI 1 start, capabilities `0`.
- `250130.155`: passive registry snapshot succeeded; direct-access field remained null and registration remained unavailable.
- `250134.969`: `experimental_registry_getter=ok`, registry `0x762ef5ddb8`, profile `bedrock-1.26.50.4-arm64`, `abi=arm64_no_arguments_return_pointer`, `mutation=none`.
- Experimental module load returned `0` in the same PID. Process remained alive; an in-world screen was captured at `build/evidence/experimental-registry-probe-current.png`. No world controls were used.
- Focused log: `build/evidence/experimental-registry-probe-1.26.50.4-m51.log`.

## Registration candidate and limits

The exact ELF contains candidate function range `0x10487d40..0x1048f1b0`. Its code references duplicate block-ID and missing-ID diagnostics. A recorded caller at `0xfb3c1ac` passes values in `X0`–`X3`; the callee allocates and locks. This makes it a plausible block-definition pipeline candidate, not a verified registration API. Argument meanings, ownership, insertion side effects and initialization timing remain unknown. It was not invoked or hooked.

A bounded Ghidra import used `-noanalysis`; targeted disassembly/decompilation did not complete within the investigation window and was stopped. No full-binary Auto Analysis ran. No gameplay mutation, native custom block, placement or save/reload was tested.

## Dynamic callsite trace attempt — 2026-10-16

LLVM disassembly reconfirmed `0xfb3c1ac: bl 0x10487d40` (`0x94252ee5`). The caller immediately sets `x0=x21`, `x1=x22`, `x2=x20`, and `x3=sp`; no argument semantics were inferred. The ELF SHA-256 remained `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

An experimental-only, exact-profile callsite breakpoint was added. It validates the loaded engine profile, executable mappings, exact call instruction and decoded target before patching. Its handler records raw `x0`–`x8`, `sp`, incoming `lr`, thread ID and monotonic time; it does not dereference arguments. The handler redirects to the original target with `lr=callsite+4`. Mod stop restores the original `BL` and emits captured records. The host attempts installation after the game's original `onCreate` returns, and the Mod retries idempotently.

The ARM64 Android build completed. Experimental APK SHA-256: `2670d73dd754494a6a9f2a5aa6f9a10a847a552cc57a8c158f3137cbe97e90ce`. On M51, process `dev.hybridmcpe.launcher:game` PID `12201` exited at `2026-10-16 20:07:33.267`, Android exit reason `SIGNALED`, status `11` (SIGSEGV). Bedrock logged a Breakpad dump at `.../data/crash/1d80789c-84e6-4b4a-86e0-87c4176b6dcc.dmp`. Captured logs contained no `candidate_trace=hit` evidence; they do not establish whether the breakpoint installed or fired. Android reported no retrievable exit trace, and the minidump remained inaccessible to ADB because the host package is not debuggable. Therefore no candidate invocation or argument observation is claimed, and the crash cause remains unknown.

The device was restored to the prior M51-tested experimental APK (`dist/HybridMCPE-experimental-native-1.26.50.4.apk`, SHA-256 `cb215097e6178ad8f751b6aff748c715c3076bc132cd7080030e0c5567571c91`) using replacement install of the HybridMCPE host only. Minecraft's installed app was not changed. The trace-enabled APK and logs remain at `dist/HybridMCPE-experimental-calltrace-1.26.50.4.apk` and `build/evidence/candidate-calltrace-1.26.50.4-m51.log`.
