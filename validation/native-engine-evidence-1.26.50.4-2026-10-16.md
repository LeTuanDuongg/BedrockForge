# Native engine evidence — Minecraft 1.26.50.4 ARM64

## Target and tooling

- ELF: `build/target-apk-analysis/26.50.4/libminecraftpe.so`
- SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`
- Format: ELF64, little-endian, `ET_DYN`, AArch64.
- Toolchain used: Android NDK `28.2.13676358` `llvm-readelf`, `llvm-dwarfdump`, and `llvm-objdump`.
- Existing 1.26.52.3 profiles and Ghidra projects were not changed.

## Ghidra failure diagnosis

The Ghidra 12.1.4 headless run was imported into a separate 1.26.50.4 project. Its log repeatedly reports `LSDACallSiteTable` errors: “Function body does not contain the landing pad” and “Function body does not fully contain the call site area.” One rotated log contained 509,453 such errors. Ghidra also reported `GccExceptionAnalyzer` failing to disassemble at `0x12b10e68`.

LLVM independently verifies `.eh_frame`: `llvm-dwarfdump --eh-frame --verify --quiet` exits 0, and its frame parser enumerates 754,234 FDEs. Thus the observed failure is in Ghidra's LSDA/call-site analysis path; it is not evidence that the ELF's `.eh_frame` is corrupt. The exact reason Ghidra derives invalid landing-pad/function ranges remains unproven.

`0x12b10e68` lies in `.data.rel.ro` (`0x129c8440` onward), not in an executable section. Its `R_AARCH64_RELATIVE` relocation has addend `0x2a92d64`, a `.rodata` address. It is not a code target. `.gcc_except_table`, `.eh_frame_hdr`, and `.eh_frame` contain no dynamic relocations in this ELF; the relevant dynamic relocation targets are concentrated in `.data.rel.ro`.

Workflow change: use `llvm-readelf` to establish PT_LOAD/section mapping, `llvm-dwarfdump` to validate and map FDE ranges, and bounded `llvm-objdump` disassembly around candidate references. Do not wait for full Ghidra analysis before collecting this evidence. The integrated REA Ghidra provider on this Windows host does not accept ELF/ARM64; the local headless Ghidra installation does import it but its broad LSDA analysis is noisy and unfinished.

## Version-pinned block-registry observations

Addresses below are ELF virtual addresses from this exact SHA-256. FDE intervals are frame-table ranges; they identify the containing code range but do not supply a C++ method name.

| Evidence | Address/range | Observed fact | Limit |
|---|---|---|---|
| Direct-access field diagnostic and accessor | FDE `0x103e71ac..0x103e7220`; string refs at `0x103e71d4..0x103e71e0` | ARM64 assembly loads a pointer from `[x0,#0x130]`; null takes a logging/failure path; non-null returns the member pointer in `x0`. It references `mDirectAccessBlocks` and “Tried to use the direct access blocks without initializing them.” | Consistent with a getter, not registration. `this` class, field semantics, owner lifetime, initialization phase, and safe callers are unknown. Do not invoke it. |
| Existing-block ID conflict check | FDE `0x10487d40..0x1048f1b0`; string ref `0x1048c580` | Instructions compare a stack-held value with a halfword loaded from `[x0,#0x14e]`; mismatch passes the vanilla-block error string to a logging call. | Establishes a validation/error branch, not a registration API or a verified object layout. |
| Existing-block material warning | FDE `0x1048f1b0..0x1048f3d4`; string ref `0x1048f310` | ARM64 code supplies the warning string and adjacent values to a call, then follows cleanup paths. | Diagnostic path only; callee semantics and object ownership are unknown. |
| Missing-ID/data-driven block warning | FDE `0x1048f3d4..0x1048f5dc`; string ref `0x1048f518` | ARM64 code supplies the “no blockID” diagnostic string to a call and follows cleanup paths. | Diagnostic path only; does not establish a custom-block creation or registration contract. |
| Block-definition requirement | FDE `0xec39110..0xec3bd90`; string refs `0xec39bb4..0xec39bc0` | Code checks a value returned through an indirect call and emits “We need a BlockDefinitionGroup in order to run the server” with `level->getBlockDefinitions()` in the diagnostic. | Supports that server startup requires a block-definition group. It does not reveal registration ABI, ownership, or finalization. |

The reference pairs above were found by decoding ARM64 `ADRP` plus `ADD (immediate)` instructions and confirmed with NDK `llvm-objdump`. FDE ranges came from LLVM's `.eh_frame` parser. No 1.26.52.3 address was reused.

One direct call edge is observed: code at `0xfb3c1ac` calls `0x10487d40`. Immediately before the call, the caller sets `x0=x21`, `x1=x22`, `x2=x20`, and `x3=sp`. This matches the Android AArch64 AAPCS64 argument registers. The argument meanings and pointee lifetimes are unknown. The target function has a large stack frame and performs allocation, locking, and cleanup; it is not safe to call as a probe.

The ELF has no `.symtab`; `.dynsym` has 91,676 entries. It does not export names matching `BlockTypeRegistry`, `BlockDefinitionGroup`, `ItemStack`, `Level`, or `Player`. The exact `JNI_OnLoad` symbol exists at `0x852e0e0` (288 bytes), but it is loader initialization, not a gameplay observation API.

## Integration decision

No native gameplay interaction is verified yet. In particular, there is no proven read-only player/world getter, engine event callback, callable block-registration contract, or safe way for the diagnostic mod to obtain a live owning object. The existing runtime therefore keeps gameplay capabilities disabled. No engine function was called, and this pass made no device or world changes.

Next useful target is a read-only world/player state path: find one engine update/event caller, establish its owning object and lifetime from the call chain, then prove its ABI before adding a gated diagnostic callback. Resume block registration only after that analysis can distinguish registry lookup, mutation, and finalization.
