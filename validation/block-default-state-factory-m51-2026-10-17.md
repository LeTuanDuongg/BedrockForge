# Default Block state producer: real M51 observation

Target: Bedrock 1.26.50.4 ARM64; exact engine SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`. Registration remains disabled. No custom `hybridmcpe` block was constructed or registered.

## Hardware observation

`tools/lldb_registry_value.py` now accepts `observe_default_state=True`. It authenticates the local ELF and runtime instruction bytes, places a hardware execution breakpoint at ELF VA `0x100fb850` **before** Cobblestone calls its base constructor, validates x0/x19 identity, then watches that exact object's `+0x200`. It changes debug state only, not engine memory. The observation is bounded and detaches in cleanup.

Use NDK LLDB with `settings set target.memory-module-load-level minimal` and `settings set target.preload-symbols false` before `gdb-remote`. Unlike the previous default `complete` attachment, this run reached the armed checkpoint promptly. The bounded frame collector now retrieves at most 16 frames instead of constructing the entire frame list first.

PID **3491**, TID **3537**, load bias `0x761e48d000`:

- Before base constructor: type `0xb4000077b6d10fb0`; watched canonical field `0x77b6d111b0`, initially zero.
- First value-changing watchpoint: stop PC ELF `0x10107428`; preceding instruction `0x10107424`: `str x0,[x19,#0x200]`.
- Assigned state `0xb400007777163030`, relocation-backed RTTI `5Block`, owner `+0x68` matches watched type. Type RTTI is `16CobblestoneBlock`.
- Stack includes `0x103e3028`, `0x92e7d64`, `0x92e6308`. Detach succeeded. No zero-to-zero watchpoint event was delivered; this does not prove the constructor skipped zero initialization.
- The JSON retains the original run's default `identifier=minecraft:air` metadata label; the actual breakpoint, object RTTI and field evidence above identify Cobblestone. The script now labels this mode correctly.
- Android later killed PID 3491 with `remove task:remove-task` at 17:46:41. No successful HUD validation is claimed for that debugger run; its screenshot shows loading, despite the historical `-hud-` filename.

Raw observation: `block-default-state-factory-m51-2026-10-17.json`.

## Producer and calling contract

Targeted LLVM disassembly, checked against executable PT_LOAD:

1. Registry traversal `0x103e3018` loads the real node value at `+0x48`. At `0x103e3024` it calls `0x101071e0` with x0=type and w1=context.
2. `0x101071e0` builds/resizes the state's pointer vector at type `+0x1e8`, with count derived from `1 << type[+0x1a4]`. This field's semantic type is not fully recovered.
3. `0x1010738c`: engine `operator new(0x130)`; `0x10107394..0x101073a0`: x0=allocation, w1=ordinal, x2=type; call state constructor `0x101f95a0`.
4. Constructor sets the known Block vtable and stores x2 at state `+0x68`. It also consumes initialized type component fields at `+0x388/+0x390`; a raw fabricated type is insufficient.
5. `0x101073ac` calls `0x101fc434(state, context)`. Its complete initialization semantics remain unresolved.
6. `0x101073bc` stores the constructed state in the type's vector. Replaced non-null states go through virtual deleting-destructor slot `+8`; this supports engine ownership by that vector, not an ownership-transfer API for mods.
7. `0x10107420` calls type vtable slot `+0x410`; `0x10107424` stores the returned state as primary. The slot's full selection contract is not yet established.

The producer is a **post-construction registry-wide population pass**, not Cobblestone's base constructor. The watchpoint was armed early enough to establish this distinction.

## Native C++ observation without debugger

Added two exact-hash/prologue-gated, forward-only experimental observers in `native/experimental_call_trace.cpp`: population `0x101071e0` (observed GP2) and Block constructor `0x101f95a0` (observed GP3). Only recorded Cobblestone instances produce bounded detailed logs. Original return registers are forwarded; no arguments, registry entries or state data are changed. Cleanup includes both hook handles and retains handles on unhook failure.

PID **6365**, TID **6410**, load bias `0x7619413000`:

- Recorded type `0xb4000077b6cb1d60`.
- Constructor produced `0xb4000077771367f0`, ordinal **0**, caller ELF `0x101073a4`; owner matched.
- Population returned with that same state as primary and first vector entry; observed context **0x1153c21**, caller ELF `0x103e3028`. The context's semantic meaning is not claimed.
- The same state subsequently entered two BlockHashPalette instances on TIDs 6411 and 6401. Both reported key **1741778478**, actual local vector index **6777**, valid key-to-index mapping and primary identity.
- Gameplay HUD captured in `block-state-factory-native-hud-m51-2026-10-17.png`; Save & Quit returned to the world list afterwards. No debugger was attached to this run.
- Selected original logcat records: `block-state-factory-native-m51-2026-10-17.txt`.

NDK r28.2 ARM64 compilation and APK signature verification passed. Both new prologues match the pinned ELF. The device test preceded a log-only correction from GP2 to GP3 for the constructor installation message; the corrected source was rebuilt, not reinstalled or re-tested. This distinction does not change the observed GP3 forwarding code.

Artifact SHA-256:

- Installed/device-tested APK, pulled back from Package Manager's actual path: `ee18efee91613b1441f48aa81b8c31ef618fa6e1024e148069b756f1af592423` (`build/HybridMCPE-state-factory-device-tested.apk`).
- Rebuilt APK after log-label correction: `47193ecf60f9822aba81a722e74eca931380bd51367efffd3cce604c11114764` (`dist/HybridMCPE-debug.apk`).
- Rebuilt runtime library: `d8ac13d39e08a5194fc0c6d5338b720f9167eae00a466afbb86c9e2a2cfb4091`.

The original pre-experiment launcher APK remains backed up at `build/HybridMCPE-pre-default-state-factory.apk`; no proprietary engine binaries are committed.

## Exact next registration prerequisite

The state-producing operation is now identified and observed. What is still missing is a **fully initialized, independently owned new BlockType handed into this population pass before palette construction**, including its identifier/property/component initialization and the population context contract. Calling the state constructor alone or inserting an arbitrary tree node would omit these dependencies.

Next bounded experiment: observe the existing ordinary-type producer immediately before the call at `0x92e7d60` (registry population entry `0x103e2dd4`) and compare Cobblestone's prepared component/property inputs with its constructor inputs. This establishes the handoff boundary for a new type before the already-observed state factory and palettes. Do not manufacture component objects or reuse the numeric context as an unexplained universal constant.

New hook shutdown/re-enable, custom-block placement and custom-block persistence were not tested. No stable capability or Mod ABI 1 change is introduced.
