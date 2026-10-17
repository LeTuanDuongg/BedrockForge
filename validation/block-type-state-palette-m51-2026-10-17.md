# Native block type, state and hash palette evidence

Target: Minecraft 1.26.50.4 ARM64, Samsung M51. Exact engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

## Implemented and run

The experimental native module now intercepts the bounded BlockType state append candidate and the verified BlockHashPalette append implementation. Both preserve the original arguments and raw x0 return, use ShadowHook stack scope, require the exact engine fingerprint/prologue, and only inspect selected, validated Cobblestone objects with kernel-assisted reads. Stable gameplay registration remains disabled; Mod ABI 1 is unchanged.

The first palette experiment accepted only the base BlockPalette dispatch and produced no selected hit. Relocation-backed RTTI then identified BlockHashPalette as a direct subclass. Its override calls the base vector appender, assigns a collision-resolved key, and inserts key-to-index data. The corrected experiment targets this exact override, not an arbitrary subclass.

## Observed object model

RTTI gives `CobblestoneBlock -> BlockType` (single inheritance). `Block` is a separate engine type. A stopped-process bounded snapshot in PID 16623 found one state in the type's vector, the same pointer in its primary field, RTTI `5Block`, and a backlink from Block+0x68 to the exact constructor-produced CobblestoneBlock. See `block-type-state-snapshot-m51-2026-10-17.json`.

In the corrected native experiment, PID **19952** recorded:

| Relationship | Runtime evidence |
| --- | --- |
| Constructor-produced type | `0xb4000077b6d0fa20`, thread 19998 |
| Primary Block state | `0xb400007777153a10`; validated Block vtable and matching type backlink |
| First hash palette | `0xb400007726b33f40`, thread 19998 |
| Second hash palette | `0xb40000772869e200`, thread 19990 |
| State key | `1741778478` at Block+0x11c; assignment flag 1 |
| Palette vector index | `6777` in both observations |
| Checks after original append | `last_matches=1;primary_matches=1;count_increased=1;hash_found=1;hash_index_matches=1` |

The same engine-owned state is referenced by two distinct palette objects. This distinguishes shared type/state data from palette-instance data within this initialization. It does not identify those instances as server/client or prove their lifetime across reloads.

## Exact consuming call path

The live return PC `0x76279f6a2c` minus load bias `0x7617e56000` is ELF VA `0xfba0a2c`. Its preceding instruction at `0xfba0a28` is the virtual call to the observed hash palette append method.

Targeted assembly establishes:

1. Palette callback `0xfba09b0` invokes type-state enumerator `0x1010da88`.
2. The enumerator walks BlockType+0x1e8..0x1f0 and calls callback `0xfba0a14` for non-null Block pointers.
3. That callback invokes palette vtable slot 3, observed dynamically as `0xfb8ba5c`.
4. Hash append calls base vector append `0xfb8bb48`, then reads the Block key through getter `0x101ea000` (returns Block+0x11c) and inserts its key-to-vector-index map.
5. Hash key assignment `0xfb8bf28` calls setter `0x101fc8c8`, which writes the key and assignment flag. This differs from the base palette's sequential assignment.

Only five small FDE ranges were imported/decompiled in the existing Ghidra database; a focused query inspected existing references. No full Auto Analysis or ELF-wide caller scan was performed. The state appender candidate at `0x1010d108` has static ownership-move evidence but no matching live Cobblestone hit; it is not exposed as a callable factory.

## Exact build and device result

- Installed APK SHA-256 matched the final build: `d3bdeec524f1002f444f8b844d4d1b9d713743c133c8c3f724ee1aac8c39f50c`.
- APK libhybridmcpe.so: `99adbcc11552f181366e14abe0662f3bd9b942937cb7ee6e930afd26d94e7022`.
- APK experimental module: `3fa72250dbb8200e2d38cc183dec110cb12d6439d639c303aab5bbdcdd90f34f`.
- NDK r28.2 ARM64 build and APK v3 signature verification passed. The installed build logs `abi=gp4_forwarded`, successful self-hook cleanup, and both observation hooks installed.
- Native palette hits occurred during disposable-world loading, and the actual HUD was captured afterward in `block-hash-palette-hud-m51-2026-10-17.png`. PID 19952 remained alive. The experiment only forwarded natural engine operations; it did not construct or register a block.
- ADB disconnected after the final HUD capture while an additional registry-log read was pending. The waiting client was interrupted and the existing native-hit log was preserved. The extra registry resample is not claimed as completed; this does not change the two previously saved native palette observations.
- Earlier PID 16623's LLDB attach took too long and coincided with Android watchdog/ANR dumps. The snapshot completed and detached, and that process resumed. PID 19952's successful native observation required no debugger attachment.

## Remaining registration gate

No `hybridmcpe` block was created. The now-verified contract is consumption/enumeration of an existing type's states into a hash palette. The missing producer is the engine operation that constructs and owns valid Block states for a newly constructed BlockType before palette enumeration, with the necessary identifier/material/definition dependencies.

The smallest next runtime experiment is a hardware write watchpoint on the exact newly constructed type's **+0x200** primary-state field, armed immediately after the known Cobblestone constructor, capturing the non-null writer PC/call chain and validating its Block+0x68 backlink. That will identify the actual producer (including any inlined vector insertion) without invoking an unknown factory. A fast attach/local-module setup is required before repeating LLDB; the slow attachment used in PID 16623 must not be repeated unchanged.

World placement interception, chunk/world-save block encoding, custom state construction, new identifier registration and new-hook unload/re-enable were not validated. Do not infer these from successful palette observation.

## Git access

No index.lock or competing Git process was found. The NTFS ACL grants the sandbox group read/execute on .git and grants the real user full control. Running scoped Git writes in the authorized user context restored index access without changing ACLs, deleting a lock, replacing the index, or rewriting history. Unrelated working-tree files are excluded from this experiment's commit.
