# BedrockForge framework status 0.1

| System area | Status | Boundary |
|---|---|---|
| C++20 framework core | Implemented; desktop build verified | Engine-independent services |
| Versioned C ABI and C++ helper | ABI v2 implemented; header and dynamic fixture tests added | Stable compatibility policy is still evolving |
| Native plugin discovery and lifecycle | ABI v2 loader initialized on Samsung M51 with zero modules | Native module execution in Minecraft is unverified; native code is trusted and unsandboxed |
| Dependency ordering | Runtime resolves multiple dependencies; importer requires declared dependencies installed | Import/dependency path is not device-tested; no profile controls |
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
| Android host APK | ARM64 APK built/signed/installed; Minecraft MainActivity reached on Samsung M51; loader reported zero modules | Launch flow verified; no mod was installed and no gameplay adapter was tested |
| Android launcher UI | Java Activity/Views prototype | Kotlin and Jetpack Compose migration is not implemented |
| Package manifest and archive tooling | Desktop tooling and Android `.bfmod` importer implemented | Import supports one flat native library; no repository/modpack support |
| Android plugin activation UI | SAF import and next-launch activation implemented | No profile switch, disable/remove controls, or descriptor/manifest dependency cross-check |
| JEI-like Bedrock browser | PROTOTYPE_ONLY / BLOCKED | Forge JAR is incompatible; GAL UI is unsupported and Bedrock overlay adapter is unverified |
| International Bedrock gameplay adapter | Not implemented | Game symbols, item codec, threading and transactions unverified |
| NetEase compatibility/reuse | Not supported | Research is architectural reference only |

This repository contains framework code and system tests only. Gameplay mods,
content packs and user projects belong in separate repositories/packages.
