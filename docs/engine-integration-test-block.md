# First native block integration gate — 1.26.52.3 ARM64

`hybridmcpe:test_block` is **not registered or implemented**. This audit found a concrete host-adapter shutdown defect and two useful engine observations, but no safe block-registration contract or stable authorized in-game host. Adding a registration stub or a behavior pack would not satisfy this milestone.

## Source-based execution audit

| Existing path | Actual execution context and limit |
|---|---|
| `MainActivity.java` / `android/jni.cpp` | Loads HybridMCPE in its own app. `startActivity` launches Minecraft separately. It cannot acquire another process's engine objects. |
| `native/runtime.cpp` | Loads Mod ABI 1 modules with `dlopen`, validates capabilities, serializes lifecycle calls, and retains handles on failed cleanup. No engine hook or block-registry callback exists. |
| `native/engine_bridge.cpp` | Uses `dl_iterate_phdr` in the calling process, hashes the mapped module's backing file and validates five read-only layouts. It does not acquire a registry/Level object or establish game-thread timing. |
| `bootstrap/modified_game/startup.cpp` | Dependency constructor ran in the historical clone's game process. Fingerprint/layout and mod lifecycle succeeded, but the clone exited. This route remains NO-GO. Loader-ready memory is not proof that block registries are initialized. |
| `bootstrap/preloader/adapter.cpp` | Pinned C++ registration callbacks bridge into Mod ABI 1 in whichever host process loads it. It requires the engine to be mapped; it does not itself start or authenticate Minecraft. No compatible authorized production host has been validated. |
| `native/iron_chest_container.*` | Independent 54-slot core. No Minecraft inventory, screen, block or world binding. |

Original installed package identity was checked again: `com.mojang.minecraftpe`, `1.26.52.3`, version code `982605203`, `arm64-v8a`, M51/Android 12. Its APK hash matches the retained backup; that matching APK's engine hash matches the original target profile. Full identity and stage results are in [the validation record](../validation/engine-integration-readiness-2026-10-16.json).

## Implemented safety correction

The Preloader adapter previously acknowledged unload even when `hybrid_shutdown()` retained active modules after a failed stop. It now returns failure and retains its own lifecycle state until cleanup succeeds. Exception paths also retain state. Mod ABI 1 and engine fingerprint gates are unchanged.

The real HybridMCPE runtime and failing-stop test module were exercised through the adapter on M51; engine verification alone is an isolated fixture. The existing ABI, core, native and layout/SHA suites also passed. [Validation and binary hashes](../validation/preloader-unload-safety-2026-10-16.json) distinguish these tests from gameplay. Upstream's [pinned lifecycle dispatcher](https://github.com/LiteLDev/preloader-android/blob/7ca94daedfa30d6d4c265fc9b591768e8dc1f5af/src/pl/internal/NativeModLifecycle.cpp) propagates a rejected unload callback; a complete production-host unload was not executed.

## Targeted engine evidence

The [block contract record](../targets/bedrock-1.26.52.3-arm64-block-contracts.json) and [assembly excerpt](../validation/block-registration-evidence-2026-10-16.txt) preserve addresses, provenance and unknowns.

- **VERIFIED machine behavior:** ELF `0x103e7800` has a 116-byte FDE-bounded body. It returns the pointer-sized member at input `+0x130`. The null path invokes a diagnostic helper and can write `0xdeadc0de` to null. Neither this routine nor the helper was called.
- **PARTIALLY VERIFIED identity:** references within that body name `BlockTypeRegistry::getDirectAccessBlocks()`, `mDirectAccessBlocks` and the matching source file. It is an accessor for initialized state, not evidence of registry insertion. Object size, instance acquisition, ownership, locks and valid invocation phase are unresolved.
- **PARTIALLY VERIFIED initialization dependency:** an assertion path at ELF `0x0ec39f6c` references `level->getBlockDefinitions()` and `ServerInstance::_postLevelInitialization(PostLevelArgs &&)`. A virtual call and non-null check precede the assertion. This is not a recovered callback or permission to register blocks during that phase.
- **HYPOTHESIS:** those diagnostics may help locate construction and finalization of the registry. No constructor, mutation signature or registration window has been recovered.
- **UNSUPPORTED for implementation:** native custom-block insertion, duplicate handling, factory/ownership, placement, world access and lifecycle callbacks have no verified contracts. No experimental address is exposed through the public SDK.

Analysis reused the existing copied Ghidra project, with no full Auto Analysis. The new script verifies exact target bytes, finds bounded adjacent ADRP/ADD references and temporarily disassembles/decompiles the getter within unwind bounds. All program edits are rolled back. `llvm-objdump` independently corroborated instructions; its nearest-export labels must not be treated as function names for these stripped ranges.

## Validation and next prerequisite

| Stage | Result |
|---|---|
| ARM64 native build | PASS |
| Current in-game integration initialization | NOT RUN; stable authorized host unavailable |
| Engine recognizes `hybridmcpe:test_block` | BLOCKED; no verified insertion path |
| World instantiation and interaction | NOT RUN |
| World destruction, shutdown and reload | NOT RUN |

Before any gameplay call, a host must first keep this exact engine running with HybridMCPE in the same process, preserve licensing/integrity and leave the original installation/worlds separate. Then capture PID, module identity, five-layout validation, zero capabilities and Mod ABI lifecycle while the game stays responsive. Historical transient bootstrap execution does not pass this gate.

The next engine prerequisite is a verified registry object acquisition and construction/insertion contract, including ownership, allowed initialization phase and game-thread dispatch. Only then can a small native diagnostic mod register the identifier once, verify lookup and placement in a disposable world, and observe teardown/reload. No further APK clone, launcher, block stub or Iron Chest gameplay subsystem was added here.

Reproduce the static evidence using the existing Ghidra installation and copied project: run `InspectBlockRegistration.java` with an output path, `-process libminecraftpe.so -noanalysis -readOnly`, and the repository's `tools/ghidra` script path. Set Java home and Ghidra app-data paths to the existing local build directories, as used by this validation. The script rejects the wrong engine SHA and incompatible seed/unwind bytes.
