# Android host milestone

BedrockForge owns its runtime, SDK and independent plugin format. The initial
host integration reuses the audited Levi native module boundary rather than
inventing an APK loader. `levi_bridge.cpp` is an Android-only lifecycle bridge
that starts the engine-independent runtime.

This remains an unverified development host. It launches the separately
installed Minecraft package through an isolated process and never includes game
binaries. The Java launcher can import a flat `.bfmod` archive, validate its
manifest, SHA-256 list, target ABI and required dependencies, then install the
library in app-private storage for activation on the next launch.

The lifecycle bridge accepts native module libraries from the private
`files/mods/enabled` directory, calls `bf_mod_entry`, validates ABI and
dependencies, then manages load/unload ordering. This is a low-level loading
path. Profile selection, disable/uninstall controls and descriptor-to-manifest
dependency cross-checking remain unimplemented. Native plugins execute with
game process privileges.

Bridge logical data is isolated under its mod config directory. It never writes
Minecraft world files. Profile management, crash capture, world backup and
restore remain unimplemented. Neither package activation nor launcher startup
proves that a module can alter Minecraft gameplay; the engine adapter is not
implemented.
