# Validation report

## Previous prototype baseline — 2026-10-09

The following results refer to the prototype before the repository was changed
to system-only. They are historical evidence and do not validate the current
cleanup or Android host changes.

- Windows desktop: CMake/Ninja with Clang 18.1.6 built the core and prototype
  plugin libraries; 3/3 CTest cases passed, including 303 core assertions and
  independent dynamic-library loading.
- Python: package preflight, scripting facade and local UI backend checks passed.
- Android: a development host APK was built, signed, installed and launched an
  authorized Minecraft PE 1.26.30.5 ARM64 installation on Android 12.
- That prototype included separate gameplay examples. Those examples, their
  content packs and their UI screenshots have now been removed from the GitHub
  project; their earlier game behavior is not a feature claim for this
  system-only repository.

## Current system-only source state

The repository is focused on the runtime, launcher host, public SDK, package
tools and framework tests. It does not hard-code demo gameplay plugins.

On 2026-10-10, the current development APK was installed on Samsung M51
(`RF8R31WEK5A`). The launcher started Minecraft's `MainActivity` in
`org.bedrockforge.host:game`; device logs reported `Native loader started; active
native modules: 0`. This verifies APK install and host-to-game activity startup
only. No `.bfmod` was installed and no gameplay change was tested. The Java
package importer is in the APK but has not been exercised with a package on the
phone. Profile controls, Android scripting, crash recovery and verified
world/inventory/UI adapters remain unimplemented.

## Reproduction commands

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests/unit -p 'test_*.py' -v
```

Android host builds require the pinned NDK described in
[`tools/versions.json`](../../tools/versions.json). A successful host build or
launch does not establish that community plugins can alter gameplay.
