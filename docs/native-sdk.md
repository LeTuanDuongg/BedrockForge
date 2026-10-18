# HybridMCPE Native SDK v1

`sdk/hybrid_mod.h` keeps Mod ABI 1. Its `HybridHost` tail now carries a pointer to `HybridSdkV1`; old ABI 1 modules continue reading the original prefix. SDK modules check both `abi` and `size` before reading the table.

The SDK advertises only working host features. The event bus currently delivers synchronous mod lifecycle events (`MOD_STARTED`, `MOD_STOPPING`, `MOD_STOPPED`) on the runtime caller's thread. Callbacks must be short and must not re-enter mod load, enable, disable or shutdown. Optional `hybrid_sdk_manifest_v1` metadata declares a version and up to 64 load-order dependencies; dependencies must already be active. Modules without the optional manifest keep their existing ABI 1 behavior.

Player, World, command, Item, Inventory, Entity, GUI and rendering remain unsupported. `blocks.find` now has an exact-build, read-only engine backend for 1.26.50.4 ARM64. The public SDK example returned a real vanilla default state on M51; see [live evidence](../validation/sdk-gameplay-2026-10-18.md). This validates that read-only slice, not a general gameplay release. Snapshot structs are copies, never engine object layouts or live references.

## Coverage

| API group | State | Current behavior |
|---|---|---|
| Mod lifecycle | Implemented | Existing ABI 1 start/stop/enable/disable/shutdown retained |
| Dependencies | Implemented | Optional manifest, duplicate/self/missing dependency checks, load order |
| Events | Partial | Mod lifecycle events work; Minecraft gameplay events have no verified source |
| Player | Unsupported | Read-only query returns explicit `UNSUPPORTED` |
| World | Unsupported | Read-only query returns explicit `UNSUPPORTED` |
| Commands | Unsupported | Explicit `UNSUPPORTED` |
| Blocks | Read-only slice observed on M51 | `find` reads existing vanilla BlockType/default-state metadata; registration remains `UNSUPPORTED` |
| Items | Unsupported | No verified client registration/ownership contract |
| Inventory | Unsupported | No verified live transaction/container contract |
| Entities | Unsupported | No verified client entity API contract |
| GUI | Unsupported | No verified native screen/controller contract |
| Rendering | Unsupported | No verified renderer callback contract |

## Examples and tests

`mods/sdk_lifecycle_example.cpp` subscribes to runtime lifecycle events, queries `minecraft:cobblestone` on its own `MOD_STARTED`, and logs the copied identifier, numeric type ID, state count and default-state hash through SDK logging. It uses no engine addresses. The diagnostic host packages/loads this example through Mod ABI 1. If startup precedes state population, the query explicitly fails; background/resume after entering the disposable test world invokes the lifecycle callback again. `mods/sdk_player_world_example.cpp` still handles unsupported Player/World APIs without inventing data.

`hybrid_sdk_tests` checks event delivery/filtering/unsubscribe, unsupported Player/World results, argument validation, and dependency manifest rejection cases. These are host tests, not Minecraft gameplay validation.

### Read-only Block contract

- Initialize `HybridBlockSnapshot.abi = HYBRID_SDK_ABI_V1` and `.size = sizeof(snapshot)`.
- The runtime supplies its fully SHA/profile-verified engine identity; shutdown clears it. Each query rechecks the live loaded image base, mappings, getter instructions and owner vtable, without hashing the entire engine again. The real registry getter and existing engine guard are called. The observed registry tree is traversed by identifier while guarded; the uncertified return ABI of `0x103e5648` is deliberately not called.
- Success requires the BlockType's own identifier to match, a bounded state vector, the exact Block vtable, the matching owner back-reference, and default-state vector membership. Only copied fields leave the guard.
- Appended snapshot fields are `type_numeric_id`, `state_count`, `default_state_hash`, and `default_state_hash_assigned`. Numeric ID and palette hash are **not** network/runtime IDs. Hash validity is indicated separately by the assignment flag.
- The original ABI 1 snapshot prefix is accepted; writes never exceed the caller's size. Existing function table fields and Mod ABI remain unchanged.
- Identifier strings are SDK-owned copies valid until process exit. `state_handle` is an opaque snapshot token, not a dereferenceable pointer, persistent ID, or retained engine object.
- `NOT_FOUND` means an absent key in an initialized registry. Unsupported engine, unreadable/inconsistent data or missing default state returns `UNSUPPORTED`. Failure leaves the output unchanged.
- `HYBRID_SDK_CAP_BLOCKS` denotes read-only lookup support on the admitted engine, not readiness at every lifecycle point and not registration support. `register_block` remains disabled.

Run `tools/test-host.ps1` for four portable native host suites and Python tests without ADB. `hybrid_sdk_block_api_tests` is explicitly a **boundary fixture**, not engine proof. Android-only runtime/loader/self-memory suites still require a device. Live acceptance requires both `sdk_block_engine=read` and `sdk_block_lookup ... verified=1` in the same real game process, followed by stop/resume and Button Duration regression checks.

## Architecture references

HybridMCPE reuses API shapes only; no source implementation was copied.

- [LeviLamina API reference](https://github.com/LiteLDev/LeviLamina/blob/main/docs/main/contents/api_reference/index.md): lifecycle, events, commands, services and UI organization. It targets Bedrock Dedicated Server; client API compatibility is not established. The repository contains LGPL-3.0 and GPL-3.0-covered files, so future code reuse needs file-level review.
- [Endstone Server interface](https://github.com/EndstoneMC/endstone/blob/main/include/endstone/server.h) and [Apache-2.0 license](https://github.com/EndstoneMC/endstone/blob/main/LICENSE): useful C++ interface and plugin patterns; it is a server runtime, not Android client integration.
- [BedrockTools public header](https://github.com/QYCottage/BedrockTools/blob/main/include/bedrocktools/BedrockTools.hpp) and [root license](https://github.com/QYCottage/BedrockTools/blob/main/LICENSE): closest Android ARM64 client API shape, but version-specific client symbols may be absent. Its README and root license disagree, so no code was copied.
- [LeviLaunchroid developer guide](https://github.com/LiteLDev/LeviLaunchroid/blob/main/docs/guide/developer.md), [compatibility](https://github.com/LiteLDev/LeviLaunchroid/blob/main/docs/guide/compatibility.md), and [Apache-2.0 license](https://github.com/LiteLDev/LeviLaunchroid/blob/main/LICENSE): native module lifecycle and packaging patterns. Its documented minimum version does not prove exact 1.26.50.4 compatibility.
