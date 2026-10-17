# Block registration trace — 1.26.50.4 ARM64

Target ELF SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

## Result

No callable native block-registration contract was established. No engine function was invoked for registration and no gameplay capability was enabled.

## Evidence-backed path

- The live registry observation already confirmed `minecraft:stone_button` resolves to a value with RTTI `11ButtonBlock`. This validates one vanilla entry and object type, not its construction or ownership.
- In the exact ELF, caller `0xfb3c06c` calls `0x10487d40` at `0xfb3c1ac`, then calls `0x10481320` at `0xfb3c224` with the same `+0xf0` subobject.
- On M51, PID `27298`, TID `27340`, the hook at `0x10487d40` fired during world loading. Its captured return PC `0x762b9851b0` matched callsite `0xfb3c1ac` plus four bytes. The original returned `0x2`; the caller's argument types and the return value's meaning remain unknown.
- The candidate's duplicate-ID branch at `0x1048c514` obtains the registry and calls `0x103e5648`, which tail-branches to the verified key lookup at `0x103e5cac`. The duplicate diagnostic follows when lookup returns an existing entry. This proves a duplicate lookup, not insertion.
- `0x10481320` iterates a list rooted at its input `+0x38` and calls `0x1048135c` for each non-null value. In `0x1048135c`, the disassembly/decompilation shows construction of a key from input `+0x50`, a call through `0x103e285c` to the registry lookup, and then either a lookup-miss path or operations on the found object, including indirect dispatch through a vtable slot. This is consumption/validation of existing definitions, not a demonstrated writer.
- The same function allocates and links entries in separate hash-cache globals around `0x135dc400`; those stores must not be classified as writes to the `BlockTypeRegistry` tree.
- A read-only scan of the existing Ghidra database found 192,688 already-decoded executable instructions. Only two `ADRP` instructions addressed the registry's page; both are getter wrappers at `0x103e2d54` and `0x103e2d60`. The database currently has no decoded direct writer reference for registry fields. This is limited coverage, not proof that no writer exists in the ELF.

## Remaining contract

The missing evidence is the concrete call edge that takes a newly constructed block definition and inserts/owns it in the live registry, plus its required state/runtime-ID initialization and registration phase. The current caller chain only checks and consumes existing registry entries. Until that producer-to-writer relationship and ownership/thread contract are recovered, creating `hybridmcpe` would require guessing an ABI or mutating the tree unsafely.

## Self-hook check

Logcat for PID `27298` shows a successful self-hook at `12:18:39` (`proxy_hits=1`, `original_calls=1`, cleanup verified), then failed self-hook behavior checks at `12:23:51` and `12:25:15` (`return=42`, both counters zero, `errno=0`). The engine candidate hook was separately installed in that process and later hit on TID `27340`, with the original returning normally. Therefore the failure is a real repeated self-hook test failure, but it does not disprove the separately observed engine interception. Its cause is unresolved; no code change was made to mask it.

## Reproduction aid

`tools/ghidra/TraceRegistryGlobalRefs126504.java` performs read-only xref and decoded-ADRP scans against the saved exact-hash Ghidra program. Run it with `-noanalysis -readOnly`; it does not create functions or invoke the engine.
