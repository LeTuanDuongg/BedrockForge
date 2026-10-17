# Live registry node-to-AirBlock write, 2026-10-17

Target: Minecraft 1.26.50.4, AArch64, Samsung M51 / Android 12.
Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
Raw capture: [registry-node-value-m51-2026-10-17.json](registry-node-value-m51-2026-10-17.json).

## Device observation

NDK r28.2 LLDB attached through `run-as` to the existing debuggable experimental
host. No engine function was invoked and no engine memory was modified. Two
hardware write watchpoints were used sequentially while all threads were stopped
between them. Both watchpoints were removed and LLDB detach succeeded.

| Observation | Value |
| --- | --- |
| Game PID | 4552 |
| Writing TID / thread name | 4614 / `Thread-11` |
| Engine load bias | `0x7618025000` |
| Registry / root-field address | `0x762b4fcdb8` / `0x762b4fcdc0` |
| Newly linked node | `0x77068e9d70` |
| Root-write instruction / stopped-next ELF VA | `0x94f691c` / `0x94f6920` |
| Node value field | `0x77068e9db8` (node + `0x48`) |
| Value-write instruction / stopped-next ELF VA | `0x103e7480` / `0x103e7484` |
| Before / after value | `0` / `0xb4000077b6d0ed30` |
| Canonical value pointer | `0x77b6d0ed30` |
| Vtable ELF VA / RTTI | `0x130f20e0` / `8AirBlock` |

The root was initially zero. At the first stop, x23 equalled the root-field
address and canonical x21 equalled the newly stored root. The node's key bytes
contained `minecraft:air`; its value was still zero. The second watchpoint caught
the subsequent value assignment on the same thread. Key bytes were unchanged.
The vptr, typeinfo and bounded RTTI name chain resolved into readable engine
segments. The constructor's vptr assignment independently matches that vtable.

**Correction:** this observed registry entry references a runtime `AirBlock`
object. It is not evidence that node + `0x48` stores a `BlockDefinition` document.
The key has a hash-like prefix followed by a libc++ string representation; the
ASCII name does not start immediately after the first eight bytes.

The captured unwind PCs, expressed as ELF VAs, were:
`0x94f6920 -> 0x103e7478 -> 0x92e7968 -> 0x92e6308 -> 0xec35a7c ->
0x97e2618 -> 0x97a2c08 -> 0x97a12f8 -> 0x1282f86c -> 0x1282f514 ->
0x12856ae4`. These are stopped/return PCs, not all function entry addresses.

## Construction-to-insertion chain

Only two relevant FDE ranges were added to the existing saved Ghidra database:
`[0x92e7770, 0x92e82ec)` and `[0x103e7220, 0x103e8104)`.
Targeted disassembly and decompilation completed; no full Auto Analysis ran.
Ghidra image base is `0x100000`: subtract it from Ghidra database addresses to
obtain the ELF VAs below. Runtime address = verified load bias + ELF VA.

| ELF VA | Supported operation |
| --- | --- |
| `0x92e7964` | Calls `0x103e7220` during the observed world initialization. |
| `0x103e7254` | Calls `0x1093fdd8(0)`; dependency returned in x0, retained in x21. Its semantic type remains unknown. |
| `0x103e7300..0x103e7304` | Calls operator new with `0x3a8` bytes. |
| `0x103e7308..0x103e7318` | x0=new allocation, x1=stack string, w2=0, x3=dependency; calls `0x10209164`. |
| `0x10209174` | Constructor calls base constructor `0x101076dc`. |
| `0x10209188` | Stores vtable `0x130f20e0`, matching the live `AirBlock` RTTI. |
| `0x103e734c` | Additional initialization through `0x1010bb6c`; context/weak ownership contract unresolved. |
| Before `0x103e7474` | Forms the key from the object's name at +`0xd0`, including libc++ string handling and hashing helpers. |
| `0x103e7458..0x103e745c` | Forms registry address `0x134d7db8` in x0. |
| `0x103e7474` | Calls generic node insertion `0x94f6850`; returns node in x0. |
| `0x103e7478..0x103e7480` | Moves node to x8, loads previous node value into x0, stores constructed object x20 to `[x8,#0x48]`. This last store was observed live. |
| `0x103e7484..0x103e7490` | If previous value is non-null, invokes its virtual slot +8. Exact cleanup/destruction contract remains unverified. |

`0x103e7220` initializes multiple hardcoded built-in blocks. It is **not** a
general custom-block registration API and must not be invoked to register
`hybridmcpe`. Likewise, `0x94f6850` alone only inserts a generic node.

The parent subsequently calls `0x93c32b4`, an optional virtual callback, then
`0x94f21f4` and `0x8ccfc78`. Their order is observed in assembly. Which operation
creates states, runtime IDs and a placeable representation is not yet verified.
Do not decompile the entire large `0x93c32b4` range as a next step.

## Gameplay and stability boundary

PID 4552 emitted `Opening level` at 14:40:40.664 for disposable world
`HybirdMCPE_check` (`75XAUsEBXRI=`). The writes were captured during this loading
path, not after assuming that a menu or Activity callback implied engine readiness.
Android later recorded exit reason `10 USER REQUESTED`, status 0,
`remove task:remove-task`, at 14:41:04.853. Continuous post-detach stability of
PID 4552 was therefore not established; no native-crash cause is asserted.

The subsequent PID 5618 verified the same engine hash, loaded diagnostic Mod ABI 1,
opened that disposable world at 14:41:39.319 and logged `Player Spawned` at
14:42:32.384. The user confirmed the HUD; `build/registry-value-hud-current.png`
was preserved as [follow-up HUD](registry-node-value-followup-pid5618.png).
It shows gameplay in that subsequent process, not HUD proof for PID 4552.

## Next contract, not an unsafe registration attempt

The next missing contract is the normal block-construction handoff in the
parent's next vanilla initialization call (`0x93c32b4`), including its required
context and the subsequent state/runtime-ID finalization. A custom block cannot
be made valid by copying the AirBlock pointer or inserting an arbitrary tree node.
Ownership, initialization dependencies and finalization must be recovered before
exposing a callable native registration backend. No custom-block capability or
new engine function invocation was enabled in this milestone. Milestones A-D
(new identifier, runtime representation, placement, persistence) remain unpassed.

## Reproduction

Use the existing isolated experimental host with the exact engine, at its main
menu in a fresh process. Obtain load bias from its verified engine mapping.
Start NDK `lldb-server gdbserver --attach <PID> :5039` through the host's `run-as`,
forward that port, and attach using NDK `lldb.cmd` / `gdb-remote`.
Import `tools/lldb_registry_value.py` and invoke:

```python
lldb_registry_value.run(lldb.debugger, local_engine_path, output_json, verified_load_bias, 180)
```

The tool rejects the wrong local hash, wrong runtime getter/writer bytes, or an
already-populated root. Enter only the disposable world after `root_watch_armed`.
It captures at most the first root write and one value write, then detaches.
Host monotonic timestamps in the JSON are not Android logcat wall-clock times.
Its runtime byte checks complement the host's full fingerprint gate; they do not
independently hash every mapped runtime byte.
