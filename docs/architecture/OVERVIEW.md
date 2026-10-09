# BedrockForge system architecture

BedrockForge is an independent mod launcher and developer platform. Its public
repository contains the loader/runtime/SDK and framework tests only; gameplay
mods are developed and distributed separately by framework users.

## Layers

1. **Launcher and profile manager** — owns game target detection, isolated
   profiles, package import, dependency resolution, enable/disable, logs and
   recovery. Android can import a single native package into private storage
   and activate it on launch; profile selection and removal are not integrated.
2. **Native runtime** — version-2 plugin ABI, dynamic library discovery,
   lifecycle, owner-scoped API context, multi-dependency ordering, safe mode,
   diagnostics and cleanup.
3. **Developer SDKs** — versioned C ABI, C++ convenience layer, and a high-level
   scripting facade. The scripting facade uses value objects and opaque
   framework handles; it never exposes engine pointers.
4. **Game Abstraction Layer (GAL)** — versioned interfaces for events,
   components, registries, inventory, UI, recipes, persistence and messaging.
   Adapters discover capabilities for an exact game build. Unsupported
   capabilities fail closed.

5. **Engine adapters** — target-specific bridges that are independently
   researched and validated for ABI, thread, lifetime, item codec and
   transaction semantics. No adapter is enabled merely because a symbol or
   offset appears plausible.

Native ABI details and trust boundaries are specified in
[`NATIVE_MODULE_ABI.md`](NATIVE_MODULE_ABI.md).
The local package contract is documented in
[`../development/PACKAGE_FORMAT.md`](../development/PACKAGE_FORMAT.md).

## System capabilities

The engine-independent core includes provider-owned item and recipe registries,
search and lookup, logical container storage, atomic journal writes, revisioned
transactions, event subscriptions, lifecycle cleanup, capability queries and
touch-layout pagination. These are framework services, not claims that the
Minecraft world or vanilla inventory is already integrated.

The native SDK supports trusted native plugins. Native code runs with the host
process privileges and is not a sandbox. High-level scripts use a narrower
value-based API, but the Android interpreter and policy limits are not
implemented yet. Both interfaces bind to a versioned Game Abstraction Layer and
must receive only the capabilities a verified adapter provides.

## Current boundary

Desktop core and packaging tools are the current development baseline. Android
can import a single native package and pass its library to the framework loader,
but this flow is not device-verified and does not provide a complete launcher
workflow or gameplay engine integration. The repository deliberately contains no gameplay
mod examples or user-created mod packs.

BedrockForge learns architectural lessons from Inner Core, Horizon and
Minecraft China Edition without copying or embedding their proprietary runtime
components. See [ecosystem lessons](ECOSYSTEM_LESSONS.md) and the
[capability matrix](../research/CAPABILITY_MATRIX.md).
