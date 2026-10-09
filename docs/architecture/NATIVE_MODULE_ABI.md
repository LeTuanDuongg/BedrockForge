# Native module ABI v2

BedrockForge native modules use a versioned C ABI in
[`sdk/include/bedrockforge.h`](../../sdk/include/bedrockforge.h). ABI v2 adds a
bounded list of required module IDs so the host can resolve a complete
dependency graph before calling module `load` callbacks.

Each shared library exports `bf_mod_entry`, which returns a static `bf_mod`
descriptor. The descriptor owns its strings and dependency list for the whole
time the library is loaded. The loader validates descriptor size, API version,
IDs, and dependency graph, then calls `load` in topological order. It calls
`unload` in reverse order and closes libraries only after callbacks return.
Missing dependencies, duplicate IDs, cycles, malformed descriptors, and load
failures abort startup and trigger rollback.

The C ABI passes value structures and an opaque session cookie. Callers must
not retain host-owned pointers past the callback lifetime. The scripting API
uses value objects and host-generated integer handles; the opaque native
cookie is not surfaced to scripts.

Modules are trusted native code. `dlopen`/`LoadLibrary` does not sandbox them,
and static initializers can execute when a library is opened. Package import
must validate archive paths, manifest constraints, hashes, target ABI, and
dependencies before placing libraries in the enabled profile. The Android
bridge currently accepts libraries from its private enabled directory; a
validated `.bfmod` installer and profile manager remain unimplemented.

ABI v1 descriptors are not accepted by the v2 runtime. Package manifests name
the framework API version separately from the native module ABI
(`native.mod_api`). Framework API version `1.0.0` with `native.mod_api: 2` is
the initial supported pair in the package builder.
