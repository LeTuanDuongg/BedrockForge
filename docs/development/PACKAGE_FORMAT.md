# BedrockForge package format 1

The desktop package builder creates a flat ZIP archive with the `.bfmod`
extension. The current Android installer accepts exactly these entries:

- `bedrockforge.mod.json` — package metadata and compatibility declaration.
- One ARM64 `lib*.so` — trusted native module exporting `bf_mod_entry`.
- `SHA256SUMS.json` — SHA-256 hashes for the manifest and library.

The manifest has the exact schema enforced by `tools/packages.py`: `id`,
`display_name`, `author`, `version`, `framework_api`, `minecraft_versions`,
`dependencies`, `optional_dependencies`, `capabilities`, and `native`. The
current installer requires framework API `1.0.0`, native module ABI `2`,
Android ABI `arm64-v8a`, and Minecraft target `1.26.30.5`. The exact game build
must remain in the framework compatibility allowlist.

Build a package from a validated manifest and ARM64 shared library:

```powershell
python tools/packages.py path/to/bedrockforge.mod.json path/to/libmod.so path/to/mod.bfmod
```

On Android, choose the archive from the launcher. The installer rejects
directories, path separators, duplicate entries, unexpected contents, files
over 128 MiB, invalid checksums, unsupported ABI/API values, and missing
required dependencies. Required dependencies must already be installed.
Successfully installed libraries activate on the next Minecraft launch.

Hashes detect accidental or post-build content changes; they do not prove who
published a package. Native modules are executable trusted code with the
Minecraft process's privileges. Install only packages from developers you
trust. Package signing, modpack archives, assets, configuration schemas,
profile selection and uninstall controls are not implemented yet.
