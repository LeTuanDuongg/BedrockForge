# Android host milestone

BedrockForge owns its runtime, SDK and independent plugin format. The initial
host integration reuses the audited Levi native module boundary rather than
inventing an APK loader. `levi_bridge.cpp` is an Android-only lifecycle bridge
that starts the engine-independent runtime.

This is a loading prototype, untested on device. It does not launch Minecraft
itself and is not yet a standalone Android application. A standalone host must
adapt/fork permitted Levi launcher modules, retain licenses, and validate game
startup with a legitimately installed game before shipping. No game binary is
included. Core libraries must resolve from the package directory; validate this
on Android before calling packaging successful integration.

The current bridge starts without plugins. Android package discovery, profile
management, import UI and dependency activation are not integrated. Native
plugins, once enabled, execute with game process privileges.

Bridge logical data is isolated under its mod config directory. It never writes
Minecraft world files. The launcher handles import, isolated versions, world
backup and restore. BedrockForge-specific profile management, crash capture,
import UI and standalone APK remain unimplemented.
