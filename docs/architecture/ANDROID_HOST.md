# Android launcher host prototype

The Android application is an experiment for starting a user-installed
Minecraft PE build inside a host-owned isolated process. It uses a pinned
version/ABI/hash target and does not modify the original game APK. The host APK
contains BedrockForge-owned code only; Minecraft engine binaries are read from
the installed game at runtime and are never bundled or redistributed.

The JNI bridge starts the engine-independent runtime and keeps game-facing
capabilities disabled unless a target adapter has passed its verification
requirements. There is no hard-coded gameplay plugin list in the host.

## Not yet a complete mod launcher

The current prototype can import a native package, but does not yet provide:

- profile selection and mod removal UI on Android;
- persistent multi-profile management and dependency-aware activation controls;
- Android scripting interpreter or script lifecycle/quotas;
- verified game tick, block interaction, item registry or inventory adapters;
- safe enable/disable recovery for arbitrary third-party plugins.

The package importer has not yet been exercised with a `.bfmod` on device.
Native plugins are trusted code with host process privileges. The exact target
is not a promise of support for other Minecraft versions.

Levi is documented as a possible lifecycle integration point only. The
optional bridge remains a research prototype and must retain upstream license
notices if it is ever distributed. No proprietary runtime from Levi, Horizon,
Inner Core or NetEase is embedded in this project.
