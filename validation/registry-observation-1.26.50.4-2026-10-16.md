# Native registry observation — 1.26.50.4 ARM64

This milestone is passive registry ownership validation, not block registration.

- Engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Tested host APK SHA-256: `04ac98ce90869582b97945cee6bb9df9d68ffbe5ff552853abcea52d668a3f27`.
- Samsung M51 game process: PID 4849, `dev.hybridmcpe.launcher:game`.
- At 17:48:46.797 the diagnostic mod started with ABI 1, capabilities 0.
- At 17:48:51.581 its native snapshot returned `registry_observation=ok`, owner RVA `0x134d7da0`, registry RVA `0x134d7db8`, direct-access pointer zero, `callable=0;registration=unavailable`.
- The exact engine fingerprint passed. The real Minecraft menu is captured in `build/evidence/registry-v2-menu.png`; timestamped process logs are in `build/evidence/registry-v2-game.log`.

## Binary evidence

LLVM executable disassembly of getters `0x103e2d54` and `0x103e2d60` gives `adrp x0,0x134d7000; add x0,x0,#0xdb8; ret`. Both return the embedded registry at owner + `0x18`.

RELA at `0x13104300` resolves owner RTTI `0x13104318`; its name pointer resolves `0x339eb9c`, containing `N7Bedrock5OwnerI17BlockTypeRegistryEE`. Owner vtable address point is `0x13104308`.

The `.init_array` slot `0x13275528` resolves initializer `0x104480b0`. At `0x10448180` it calls constructor `0x103e286c` on owner + `0x18`; at `0x104481a0` it registers owner destructor `0x103e2640` through `__cxa_atexit`. That destructor calls embedded-registry destructor `0x103e2b08`.

Constructor callers allocate `0x158` bytes. Constructor `0x103e296c` zeroes offsets `0x130..0x14f`. Getter `0x103e71ac` reads offset `0x130`, with a fatal null branch; it is never called by this diagnostic.

The diagnostic independently validates mapped segment permissions, exact getter instruction bytes, owner vptr, RTTI and type name. It reads through `process_vm_readv(getpid())`, samples the field twice, and never dereferences the resulting pointer or calls an engine function. A stable sample is not proof of registry readiness or synchronization.

## Validation and limits

NDK 28.2 ARM64 build with the pinned Preloader adapter succeeded. Native tests and self-memory valid/inaccessible/overflow/null/empty-read tests passed on M51 in the isolated shell test environment. The app-process snapshot separately passed in PID 4849. The initial `/proc/self/mem` reader was denied in the app; the platform self-read API replaced it without a raw-pointer fallback.

No new world interaction or block placement was tested in this milestone. Mod ABI 1 remains unchanged, gameplay capabilities remain disabled, and `hybridmcpe:test_block` does not exist in-game yet.

Registration remains blocked on the actual insertion contract: definition/factory construction and ownership, argument/return ABI, initialization window and engine thread, and finalization/persistence handling. Diagnostic strings and registry getters do not establish these contracts. No callable registration address is enabled.

## Bounded registration-path follow-up

The following caller chain is from this exact 1.26.50.4 ELF, corroborated by bounded LLVM disassembly. All listed code RVAs fall inside its executable `PT_LOAD` range. This is a candidate path, not a proven vanilla block-registration call chain.

- At `0xfb3c0b4`, a caller obtains the global registry through `0x103e2d54`, then calls `0x103e3cd0` at `0xfb3c0c8`. The latter reads the registry's `+0x148` state under a reference-counted lock and traverses a linked collection, invoking a stored callback indirectly through a node field. The callback's semantic type and lifetime are unknown; this is not approved as a mod callback interface.
- The caller then makes two virtual calls through its own object and passes the `+0xf0` subobject, one virtual result, an incoming argument and a stack-held pair to `0x10487d40` (`0xfb3c1ac`). It calls `0x10481320` with that same `+0xf0` subobject at `0xfb3c224`.
- `0x10481320` traverses a collection rooted at object `+0x38`; for each node it loads a value at `+0x28` and calls `0x1048135c`. That routine gets the global registry (`0x103e2d60`), derives a key from input `+0x50`, then calls `0x103e285c`. `0x103e285c` clears three control arguments and tail-branches to `0x103e5cac`; the latter locks state at registry `+0x148` and performs a key-tree search. This supports lookup/use of existing entries; it does not show insertion.
- Candidate `0x10487d40` contains the duplicate block-identifier diagnostic at `0x1048c580`. In that path, `0x1048c514` obtains the global registry, and `0x1048c520` calls `0x103e5648`, which likewise clears control arguments and tail-branches to the tree-search routine. This is direct evidence of duplicate checking against existing registry data in this candidate, not proof that the candidate inserts a new block.

At the real menu snapshot in PID 4849, registry ownership/type checks passed but the field at registry `+0x130` was null. This is consistent with its accessor's observed null/error path. It establishes only that this particular direct-access field had not been initialized when sampled at the menu; it does not establish the full registry's readiness, a freeze point, or the registration window.

## Registry-field writer follow-up — 2026-10-16

The exact engine file was re-hashed before inspection: SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`. Its executable `.text` range is VA `0x64c63c0`, size `0xc44aa6c`; the candidate instructions lie inside that range.

Bounded LLVM disassembly confirms three distinct operations on registry `+0x130`:

- Constructor `0x103e286c` zero-initializes the field at `0x103e296c`.
- Destructor `0x103e2b08` loads it at `0x103e2b68`, clears it at `0x103e2b6c`, then conditionally calls `operator delete` at `0x103e2b74`. This establishes cleanup of the stored allocation, not the pointed-to engine object's ownership.
- Function `0x103e714c` calls lookup helper `0x103e5cac` with the incoming object in `x0`, a static key address `0x134c8298` in `x1`, and three zero flags. It allocates 8 bytes, stores the helper result in that allocation, replaces `[x19+0x130]`, and deletes the previous 8-byte holder if present. This is a cache/holder write around a lookup result; no block-definition construction, identifier insertion, state assignment, or registry-tree mutation is visible in this body. The key's meaning remains unresolved.

A corrected AArch64 `BL`-immediate scan found one direct call to `0x103e714c` at `0x92e7d68`, inside the function range `0x92e7770..0x92e82ec`. The containing function references `NonOwnerPointer<Level>` diagnostics and performs Level-related object checks. This supports a Level-related cache consumer as a likely role, but does not establish its exact lifecycle phase or thread. The menu snapshot's null field remains consistent with constructor initialization and the accessor's null branch; it does not prove the registry is empty or uninitialized.

No verified safe game-state callback exists in the current diagnostic. Mod lifecycle callbacks are host/activity lifecycle events, not verified engine world-transition events. The attempted device interaction returned to the menu and did not provide evidence of a loaded world; no world, loading, or return-to-menu comparison is claimed. No engine function was called and no runtime code changed.

### Registration decision

No safe registration code is justified. The current trace does not establish which virtual implementation supplies the objects passed into `0x10487d40`, how a new block definition/factory is constructed, which operation writes it into the registry, how block states/runtime IDs are assigned, whether definitions are owned or borrowed, or which thread and startup phase may perform insertion. The later `0x10481320` pass is not proven to be finalization. Mod startup at the menu is not evidence of a pre-finalization callback. Preserve the registry observer as a prerequisite and keep all gameplay capabilities disabled until a vanilla successful insertion path and its lifetime/timing contracts are recovered.

### Targeted vanilla construction and storage trace — 2026-10-16

The exact ELF was rechecked at SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`. `llvm-readelf` maps executable `.text` at VA/file offset `0x64c63c0` (size `0xc44aa6c`); candidate instructions are within this executable range. `.data.rel.ro` begins at VA `0x129c8440` (file offset `0x129c4440`), so those data addresses were not treated as code.

The real registry lookup at `0x103e5cac` loads a root pointer from `[registry+8]`, walks tree nodes, and compares keys using data at node `+0x20`. It also reads synchronization/reference state at `[registry+0x148]`. This is evidence for a synchronized tree-based lookup. It does not show a tree insertion, establish whether values own or borrow definitions, or identify any block-registration writer. Wrapper `0x103e5648` clears three optional arguments and tail-branches to this lookup; it is not an insertion API.

The data-driven construction path has partial evidence, but no proven connection to insertion. A Cereal `BasicLoader::load` specialization for `BlockDefinition` is named in read-only data and referenced from code in FDE `0xb1df98c..0xb1dfcec`; its callers reach that loader. A `Puv::SlicedLoader<...BlockDefinitionDocument...>::_resolveCustomParseData()` name is referenced from FDE `0x10487d40..0x1048f1b0`. `BlockDefinitionGroup::initBlockTypeFromDefinition`, `initBlockFromDefinition`, and `buildBlockSchema` appear in closure/type-name metadata. These names support parsing/schema/definition processing, but are not symbol addresses and do not prove call order, object construction ABI, or registry mutation.

The `Vanilla block %s already registered with a different ID` path at `0x1048c580` calls the registry getter (`0x103e2d54`) and lookup wrapper (`0x103e5648`) before comparing an ID. The missing-ID diagnostic at `0x1048f518` says fully data-driven vanilla blocks are not yet supported in that path. These are validation/lookup paths, not evidence of insertion. A separate `Failed to register Block with identifier: %s` diagnostic is referenced in a JSON-oriented validation function; its body has no direct call into the registry code region. It is not enough to identify a writer.

The APK's vanilla behavior-pack archive contains 97 embedded `*.block.json` path names. Its custom archive payload was not parsed and its runtime consumer was not traced. This establishes that block data files are present, not how the engine constructs/registers their definitions. Embedded strings resembling block identifiers likewise do not establish a registration call chain.

**Correction:** the earlier statement that `0x103e714c` had no direct caller was wrong. The scan compared signed JavaScript integers and discarded ARM64 `BL` encodings; the unsigned scan found the caller at `0x92e7d68` above. This correction changes the cache-writer reachability finding only. It does not turn that cache update into a registration operation.

**Result and blocker:** neither the vanilla-definition trace nor the registry-storage trace reaches an instruction that inserts a new `BlockType`/definition into the tree rooted at `registry+8`. The missing evidence is the producer/writer that links a newly created definition into that tree, plus proof of definition/factory ownership, state/runtime-ID assignment, finalization boundary, and the safe initialization thread/phase. Until that writer and its callers are identified, no callable registration ABI can be stated safely; registration remains disabled. No engine calls, hooks, runtime changes, APK changes, or device tests were performed for this targeted pass.

### Saved-database caller audit — 2026-10-16

The existing Ghidra project was queried read-only against the exact ELF. A targeted ARM64 `BL` scan found 1,062 direct callsites to lookup `0x103e5cac`, assigned to 34 FDE ranges. FDE membership is only a search bound; it is not treated as proof of a C++ function boundary. A new read-only Ghidra script mapped those callsites to existing function bodies: only the already imported cache writer `0x103e714c` contains a defined caller reference in the database. The other callsites are not yet represented as Ghidra cross-references, despite the project's broader function count. The saved database therefore cannot yet provide a complete caller graph for this lookup.

LLVM disassembly of nearby executable ranges supplies concrete lookup-use evidence:

- At `0x103e8678`, code passes the verified global registry address `0x134d7db8` in `x0`, a key from an input array in `x1`, and zero control arguments in `x2..x4` to the lookup. A non-null result is appended to an output pointer array. The surrounding range also allocates/grows that output array; this is not evidence of tree-node allocation.
- In ranges `0x103e89e0..0x103e8e18` and `0x103e8e18..0x103e9194`, repeated calls pass the same global registry address and static keys; returned pointers are stored in an output array. These are lookup/collection consumers, not writers of the tree rooted at `registry+8`.

The lookup body supports only a tree-shaped search representation: root at registry `+8`; key comparison data at node `+0x20`; search follows one of two pointer fields at node offsets `+0` or `+8`; a matched entry yields a pointer-like value at node `+0x48`. The node type, exact tree implementation, mapped-value ownership, allocator, and insertion operation remain unknown. No node-link write or registry-root write was established by these callers.

**Outcome: verified prerequisite, not registration.** The registry's read path and several global-registry lookup consumers are now grounded in exact-version assembly. The specific missing edge is still the producer that takes a constructed block definition and writes a node into this tree; the current Ghidra database lacks caller boundaries for nearly all direct lookup references, and no confirmed writer or loader-to-writer edge is known. Do not implement or invoke a registration API until that writer and its ownership/lifecycle contract are recovered. The new script is `tools/ghidra/TraceRegistryLookupCallers126504.java`; its output is `build/ghidra-fde-registry-1.26.50.4/lookup-callers-functions.txt`.

### Registry tree teardown evidence — 2026-10-16

The exact ELF was opened and its SHA-256 matched the target profile. Bounded LLVM disassembly establishes a concrete teardown path:

- Registry destructor `0x103e2b08` loads `[this+0x8]` into `x1`, places the same registry object in `x0`, then calls `0x104381e4` at `0x103e2c94`.
- `0x104381e4` treats `x1` as a node. It recursively calls itself with child pointers from node `+0x0` and `+0x8`, then loads node `+0x48`, clears that field, and invokes the pointed object's vtable slot `+0x8`.
- It checks bit 0 of byte `node+0x28`; when set, it frees the pointer at `node+0x38`. It then frees the node itself. The recursion is post-order and covers both child links.

This proves the registry destructor delegates destruction of the tree rooted at `registry+8`; the tree teardown invokes a virtual cleanup operation for each non-null mapped pointer and frees each node. It also supports that the node/key representation may own auxiliary key storage. It does **not** prove exclusive ownership of mapped objects, because the virtual operation may release shared or intrusive ownership. It does not reveal balancing metadata or prove `std::map`/`std::__tree`; two child links alone fit a binary tree, but no insertion rotations, color bit, or parent-link update has been tied to this registry. Treat an internal balanced tree and libc++ map-like layout as hypotheses only.

The prior lookup evidence still establishes search by node `+0x20` and return of the pointer-like value at `+0x48`. The destructor adds lifecycle/cleanup evidence, not insertion. No instruction has yet been shown linking a newly allocated node into the live root or updating a parent/child link on this registry instance. No loader-to-writer edge, `BlockDefinition` ownership contract, state/runtime-ID phase, or safe registration thread is established. Therefore no registration ABI or experimental mutation is enabled.

**Next bounded step:** identify a writer by following a concrete vanilla block definition object from its construction site and proving that same object reaches an insertion or link update on the global registry. Generic stores at matching offsets and generic tree helpers are insufficient. If this edge is not recovered, stop this tree-writer search and use a smaller, read-only real-engine observation as the next native milestone; the current safe boundary is the existing registry snapshot, not a gameplay callback.

### Controlled M51 live-world registry attempt — 2026-10-16

The experimental delay was raised from 45 to 180 seconds in `mods/experimental_registry_probe.cpp`, then built and installed as the HybridMCPE host. The modified host APK SHA-256 was `01cbe60d763dd45d89dcade1d01a1bb4ded2b8bfda772d87873bc7027d1ea4e9`. The original Minecraft application was not reinstalled or modified. The user confirmed that `My World` in the isolated HybridMCPE profile is disposable; it was saved and quit normally.

The final run used game PID `7170` (`dev.hybridmcpe.launcher:game`) on Minecraft `1.26.50.4` ARM64, with engine SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`. The native diagnostic started at `23:02:10.852`; the registry getter and self-hook test completed, with the hook self-test reporting `proxy_hits=1`, `original_calls=1`, and verified cleanup. Probe scheduled at `23:02:20.554` for 180 seconds. Screenshot `build/m51-worldproof5.png` was captured at `23:05:14` and still showed the Minecraft Play/Worlds list. Lookup began at `23:05:20.695` on worker TID `7378` and completed at `23:05:25.536`; therefore this run did **not** observe the registry after a confirmed world load.

The read-only getter returned registry RVA `0x134d7db8` with `abi=wrapper_x0_registry_x1_key_verified`. Neither guessed key `minecraft` nor `minecraft:stone` was found. The output reports `key_layout=libcxx_string_inferred`; the identifier representation is not confirmed. `mutation=none`. This does not prove vanilla block definitions are absent, nor establish that the returned registry belongs to a live world context. The later screenshot `build/m51-hud.png` at `22:53:46` showed a real gameplay HUD, but it was captured after the earlier lookup at `22:52:52`; it cannot validate that lookup's gameplay timing.

**Outcome:** no live-world registry result yet. The exact blocker is lack of a verified world-loaded callback or reliable automated activation of the existing world card before the fixed-delay probe fires. The fixed delay is host/mod-start-relative and is not evidence of world readiness. Keep the registry access read-only and gameplay capabilities disabled. The current tested key guesses and inferred string layout cannot support a claim that vanilla blocks are missing or identify a `BlockDefinition`.

**Later result (same date):** the timer was removed and an explicit in-world sample succeeded after correcting ARM64 pointer-tag handling. Four real vanilla identifier/RTTI pairs were observed. See [the controlled live-world result](live-world-registry-explicit-1.26.50.4-m51-2026-10-16.md); the earlier timing failure and guessed-key misses above remain historical evidence, not the current outcome.
