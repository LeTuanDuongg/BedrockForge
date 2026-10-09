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

The repository is being refocused on the runtime, launcher host, public SDK,
package tools and framework tests. It no longer hard-codes demo gameplay
plugins into the Android host. This refocus has not yet been rebuilt or tested.

The Android host remains a prototype for one pinned target, not a released
launcher. Package discovery/import and profile UI, Android scripting, crash
recovery, and verified world/inventory adapters are not implemented. No gameplay
capability is claimed as working by this repository snapshot.

## Reproduction commands

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests/unit -p 'test_*.py' -v
```

Android host builds require the pinned NDK described in
[`tools/versions.json`](../../tools/versions.json). A successful host build or
launch is not evidence that arbitrary community plugins can yet be imported or
run.
