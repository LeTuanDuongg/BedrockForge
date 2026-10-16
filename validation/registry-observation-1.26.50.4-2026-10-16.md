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

### Registration decision

No safe registration code is justified. The current trace does not establish which virtual implementation supplies the objects passed into `0x10487d40`, how a new block definition/factory is constructed, which operation writes it into the registry, how block states/runtime IDs are assigned, whether definitions are owned or borrowed, or which thread and startup phase may perform insertion. The later `0x10481320` pass is not proven to be finalization. Mod startup at the menu is not evidence of a pre-finalization callback. Preserve the registry observer as a prerequisite and keep all gameplay capabilities disabled until a vanilla successful insertion path and its lifetime/timing contracts are recovered.
