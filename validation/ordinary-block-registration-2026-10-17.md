# Ordinary block construction and registry handoff on M51

Target: Minecraft 1.26.50.4 ARM64, engine SHA-256
`fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
Baseline commits `cd2fb7b` and `35f8516` are preserved.

## Real engine capture

[Raw LLDB capture](ordinary-block-m51-2026-10-17.json): PID **15705**, writing
TID **15770** (`Thread-13`), load bias `0x761b627000`, registry `0x762eafedb8`.

The observer used hardware-only execution breakpoints on the already-verified
generic insertion entry `0x94f6850`. It admitted only calls whose x0 equalled
the exact registry instance. Seven bounded stops observed these keys in order:
air, unknown, client_request_placeholder_block, stone, grass_block, dirt,
cobblestone, all with `minecraft:` namespace. The latter name was selected
explicitly; other containers were not classified from matching offsets alone.

For cobblestone, the tool followed the real return address to obtain the node,
checked its key against the entry arguments, removed the execution breakpoint,
and watched that node's +`0x48` value field. No engine function was invoked.

| Runtime observation | Value |
| --- | --- |
| Insertion call / returned-next ELF VA | `0x93c3e44` / `0x93c3e48` |
| Node | `0x7706dba650` |
| Value-write instruction / stopped-next ELF VA | `0x93c3e50` / `0x93c3e54` |
| Store | `str x23, [x8,#0x48]` |
| Old value / new raw value | `0` / `0xb4000077b6d22c50` |
| Canonical object | `0x77b6d22c50` |
| Vtable ELF VA / RTTI | `0x130d6190` / `16CobblestoneBlock` |
| Debugger cleanup | Hardware breakpoints/watchpoint removed; detach succeeded |

The caller stack joins the value store in the vanilla initialization range
`0x93c32b4..0x94d71f4`, then return PC `0x92e7974`, and the previously observed
world-load chain. It runs during world initialization, after the special
AirBlock sequence. Activity callbacks alone were not used to infer this phase.

## Cobblestone creation-to-insertion, matching assembly

All addresses below are ELF VAs, not file offsets. Runtime = load bias + ELF VA.
Ghidra database addresses additionally include artificial image base `0x100000`.

1. `0x93c3c50` calls `0x1093fdd8(23)`; dependency retained in x20. Its semantic
   type is not assumed to be Material without additional type evidence.
2. `0x93c3c58..0x93c3c60` obtains the name from engine-owned source `0x134f1c30`
   through `0xe4c2a9c`, then copies its libc++ string representation.
3. `0x93c3c9c` calls `0x103e2784`, with registry in x0 and stack output address
   in **x8**. This is an AArch64 indirect-return dependency, not an ordinary
   two-argument C function inferred from decompiler parameter ordering.
4. `0x93c3ca4` allocates `0x3a8` bytes through `0xe49677c`.
5. `0x93c3cbc` calls constructor `0x100fb840`: x0 allocation, x1 stack string,
   w2 literal **4**, x3 dependency. The number 4 is not claimed to be a runtime ID.
6. Constructor `0x100fb840..0x100fb86c` calls base `0x101076dc` at `0x100fb850`,
   then assigns vtable `0x130d6190` at `0x100fb85c`. This matches the live RTTI.
7. Additional initialization calls `0x1010bb6c`, with context/control data obtained
   through registry +`0x138`/+`0x140`. Full ownership semantics remain unresolved.
8. Object name at +`0xd0` supplies the registry key. The code also updates a
   namespace structure at registry +`0xe0` before generic node insertion.
9. `0x93c3e44 -> 0x94f6850` creates/finds the real node. `0x93c3e50` assigns the
   constructed CobblestoneBlock, observed by the hardware watchpoint. A previous
   non-null value would receive a virtual call through slot +8.
10. After assignment, `0x93c3e80 -> 0x94f6a0c` and `0x93c3e90 -> 0xe4c2b18`
    update a second structure at registry +`0xf8`. `0x93c3eac -> 0x103e839c`
    releases the constructor scope. Further property initialization follows.

This is not an AirBlock-only path: the observed stone insertion returns at
`0x94d73c0`, inside factory **`0x94d71f4..0x94d7578`**. That factory constructs
through `0x107801ec`, assigns its object at `0x94d73c8`, and returns the object.
It has seven direct BL callers in the bounded vanilla initialization range:
`0x93c3380`, `0x9440894`, `0x9440a58`, `0x9440c1c`, `0x9440de0`, `0x9440fa4`,
`0x9441168`. This establishes reuse of that type-specific factory, not universal
binary compatibility or a single factory shared by all block types.

Dirt's observed insertion return is `0x94d77e0`, in a **different** factory,
`0x94d760c..0x94d7998`. Do not confuse the adjacent `0x94d7578` cleanup routine
with registration merely because it has many callers. Cobblestone's sequence
is inlined into the large vanilla initializer rather than the stone factory.

## Exact remaining registration dependency

The missing contract is the handoff from a newly constructed block and its
constructor-time numeric identifier to complete engine state/runtime-ID tables.
Node insertion demonstrably does not finish registration: namespace/name indexes,
shared context, constructor scope and per-block initialization also participate.
No valid new numeric-ID allocation or finalization operation has yet been
established for an additional object. A hand-inserted node would not test this.

Scope helper `0x103e2784` reads registry +`0x148`/+`0x150`, uses x8 for output,
and calls `0x103e82d4`. Cleanup `0x103e839c` clears a byte through a referenced
context helper and releases reference-counted state. That byte's registration semantics
are not established. The Ghidra pseudocode for `0x103e2784` escaped the FDE range
through misclassified tail calls and is rejected as a full callable contract;
bounded LLVM assembly is the evidence used here.

Five relevant small boundaries were saved into the existing Ghidra project;
no full ELF analysis or whole-function decompilation of the large vanilla
initializer ran. The new LLDB path tests the specific ordinary-object handoff;
it is a diagnostic prerequisite, not a BlockRegistration backend. No custom
object was fabricated and no custom registration was invoked. Milestones A-E
for `hybridmcpe` remain unpassed; stable gameplay capability remains disabled.

Reproduce from a stopped NDK LLDB attachment at the menu of a fresh isolated
process, using its verified mapping load bias:

```python
lldb_registry_value.run(lldb.debugger, local_engine_path, output_json,
                        verified_load_bias, 180, "minecraft:cobblestone")
```

The observer permits only three predefined vanilla identifiers, at most 256
insertion stops, and rejects software-breakpoint fallback. Reads occur while
the process is stopped. Final tooling also compares the insertion entry bytes
against the pinned ELF; its key decoder is covered by the six offline checks.

## Real-launcher regression and limitations

NDK r28.2 ARM64 build passed. The updated isolated launcher was installed with
data-preserving `adb install -r -t`; installed Minecraft was not changed.
Known-working prior APK retained at `build/HybridMCPE-before-ordinary-block.apk`.

- Updated APK SHA-256: `842c5c0f1a268c10cd60be3040f464ab56a108236fa2495b16311b5e0e3a4031`.
  The installed base APK was pulled through `pm path` and matched this full hash.
- Prior APK SHA-256: `676acf9f9055345e38712162cdb00d791f1bed9fdbb022c203469ab322f7d5bc`.
- Native library SHA-256: `348f430bd3255bbe55fad22021065d22b63204720af6d961b52cd8b997399494`.
- [Actual launcher log](self-hook-real-host-2026-10-17.txt): PID **18061**, three
  self-hook runs passed at 15:08:35.626, 15:10:04.253 and 15:10:59.829, with
  return 42, one proxy/original call and verified cleanup each time. Two
  pause/resume cycles returned suspend=0 and resume=0. These are real-host
  hooking regressions, not new gameplay or registration success.
- Offline key decoder/fingerprint checks: **6 passed**, including inline and
  heap names, oversized-name rejection and the pinned ELF writer opcode.

PID 15705 logged opening only the disposable `HybirdMCPE_check` world. A later
ANR reported waiting 10004 ms for `FocusEvent(hasFocus=false)`; Android exit-info
then recorded `USER REQUESTED`, `remove task:remove-task`. The cause of that ANR
is not established. Do not claim post-debugger HUD/playability or stability for
that process. PID 18061 is a separate subsequent launcher run; its hook results
must not be presented as world stability evidence for PID 15705.
