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
