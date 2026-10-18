# Item registration trace — Bedrock 1.26.50.4 ARM64

Target ELF SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`  
Existing Ghidra program: `build/ghidra-1.26.50.4/HybridMCPE_1_26_50_4.gpr`  
Language: `AARCH64:LE:64:v8A`; image base: `0x100000`; 79,151 existing functions.  
Inspection used `analyzeHeadless -readOnly -noanalysis`; no full analysis pass or game modification was run.

Addresses below are **ELF VAs** (Ghidra program address minus `0x100000`).

## Observed vanilla path

The `minecraft:stick` initializer at ELF `0x0ff057c0` passes a prebuilt key and a 16-bit category value (`w3 = 0x18`) to helper `0x0ff35de4` at callsite `0x0ff08328`. A following item uses category `0x19`, so that argument is not a custom numeric ID contract.

The item key is assembled from hash `0x2720709c84a455c4` and the `minecraft:stick` string. The same construction for `minecraft:apple` uses `0x20840f07267dca7c`. Both match FNV-1 64-bit, not FNV-1a. The `HashedString`-like constructor at `0x1282d2e0` stores the supplied hash and initializes the string-bearing part; this supports the key representation but does not establish a public registration API. FNV-1 64 for `hybridmcpe:scanner` is `0x72a89c1011e30c22`, computed only as a candidate key.

At `0x0ff35de4`, the observed caller supplies an output address in `x0`, a 16-byte shared-pointer-like pair through `x1`, the key through `x2`, and the category in `w3`. The helper copies the pair, increments a 16-bit field at registry/context offset `+0x1b8`, and calls auxiliary and primary table helpers. Its downstream path at `0x0fbb1384` allocates `0x1d0` bytes, calls Item base constructor `0x0ff7cd08`, and places shared-ownership data into engine containers. The constructor writes the verified `Item` vtable. This is **statically supported as the vanilla Item creation/registry path**, but exact C++ parameter types, duplicate/error semantics and ownership transfer are not yet runtime-verified.

The one observed initializer wrapper starts at `0x0fbbf7b8`. It retains incoming `x0` as context and derives the value passed to the item initializer from its first field. Its unique direct callsite is `0x0fbbf894`; this calls initializer wrapper `0x0fcb9bb0`. In that wrapper, `x19` is copied from incoming `x0`, then a stack address containing a 16-byte pair is passed as the item initializer's `x1`. This is evidence of a startup-only context chain, not proof that the context is an `ItemRegistry` object available to mods later.

The apparent PLT call after construction resolves to `__cxa_atexit`; it is static-destructor registration, not Item registration.

## Contract status

| Contract | Status | Evidence / limit |
|---|---|---|
| Exact target and Ghidra mapping | `STATICALLY_SUPPORTED` | SHA-256 matches; AArch64 program uses `0x100000` image base. No runtime test this turn. |
| Identifier hash rule | `STATICALLY_SUPPORTED` | Two vanilla identifiers match FNV-1 64; custom hash is only computed, not engine-accepted. |
| Vanilla Item constructor | `STATICALLY_SUPPORTED` | `0x0ff7cd08` writes the `Item` vtable; constructor argument semantics remain incomplete. |
| Semantic creation/registry helper | `CANDIDATE` | `0x0ff35de4` and `0x0fbb1384` show object allocation and table updates in assembly; no runtime capture or call contract verification. |
| Context ownership/lifetime | `UNKNOWN` | Shared-pointer-like pair and startup wrapper observed; exact type/control block ownership and lifetime are unverified. |
| Safe registration phase for mods | `UNKNOWN` | Vanilla path is in initialization code. HybridMCPE loads its diagnostic after the wrapped Minecraft Activity `onCreate`; whether this precedes or follows Item initialization has not been observed. |
| ItemStack, Creative, use, persistence | `UNKNOWN` | Not reached in this milestone. |

## Next decisive experiment

On a disposable authorized launch, install one observation-only hook at the `0x0ff35de4` registration helper **before** the vanilla Item initializer runs. Capture the actual `x1` pair, key, category, caller and timing; validate the context identity and lifetime without changing arguments. If HybridMCPE cannot install that observer before this startup call, first expose an earlier host-controlled load point. Do not call the helper with a fabricated context or treat the hash alone as registration.

## Reference architecture

Inner Core's public docs show a two-step developer API (`IDRegistry.genItemID` then `Item.createItem`) and separate Creative/use helpers. Its public native headers also document a legacy toolchain dependency on GNU STL headers. These are architectural references only; they do not establish Bedrock 1.26.50.4 engine ABI or a reusable native registration call.

- <https://docs.inner-core.org/api/namespace/IDRegistry>
- <https://docs.inner-core.org/api/namespace/Item>
- <https://docs.inner-core.org/docs/items/first-item>
- <https://github.com/zheka2304/innercore-native-headers>

## Validation

`tools/test-host.ps1`: PASS — five portable native host suites and 14 Python tests. These are host tests, not Minecraft item registration evidence. No APK was installed and no game/world data was touched.
