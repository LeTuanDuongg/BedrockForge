# BedrockForge framework status 0.1

| System area | Status | Boundary |
|---|---|---|
| C++20 framework core | Implemented; desktop build verified | Engine-independent services |
| Versioned C ABI and C++ helper | Implemented; basic ABI checked | Stable compatibility policy is still evolving |
| Native plugin lifecycle | Implemented and desktop-tested | Trusted in-process code; no sandbox |
| Dependency ordering | Implemented subset | Android profile resolver is not wired into launch |
| Capability discovery and safe mode | Implemented in core | Android recovery workflow remains incomplete |
| Diagnostics and owner cleanup | Implemented subset | No production crash collector |
| Item/recipe provider registries | Implemented in core | Not a complete live Minecraft registry |
| Event bus and manual tick | Implemented in core | No verified engine tick adapter |
| Logical storage and revisioned transactions | Implemented in core | Not connected to vanilla inventory/world containers |
| Journal persistence | Implemented in core | Framework data only; migration tooling is incomplete |
| Versioned GAL/component contracts | Design and partial core support | Target-specific adapters remain disabled until verified |
| Touch layout/pagination model | Implemented as engine-independent utility | No gameplay UI integration in this repository |
| Desktop scripting facade | Implemented subset and integration-tested | Trusted prototype; not a sandbox |
| Android scripting runtime | Not implemented | Interpreter, quotas and cleanup are future work |
| Android host APK | Prototype; exact-target device launch verified earlier | Not a complete package/profile manager |
| Package manifest and archive tooling | Desktop preflight/package tools implemented | Arbitrary Android import/activation not integrated |
| Android plugin discovery/import UI | Not implemented | Required for community-created plugin workflow |
| International Bedrock gameplay adapter | Not implemented | Game symbols, item codec, threading and transactions unverified |
| NetEase compatibility/reuse | Not supported | Research is architectural reference only |

This repository contains framework code and system tests only. Gameplay mods,
content packs and user projects belong in separate repositories/packages.
