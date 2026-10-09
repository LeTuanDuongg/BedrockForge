# Android system validation checklist

Desktop tests do not replace checks on a real Android device and an authorized
Minecraft installation.

## Host and profile safety

- [ ] Verify package identity, exact engine version/hash and ABI before launch.
- [ ] Reject unknown targets before loading engine-dependent code.
- [ ] Confirm original APK and world files are never modified.
- [ ] Create, select, back up and restore isolated launcher profiles.
- [ ] Kill/restart during host preparation and verify there is no crash loop.
- [ ] Verify safe mode starts the framework without loading plugins.
- [ ] Confirm logs identify runtime/profile errors without dumping credentials.

## Native plugin loading

- [ ] Import a framework package and validate archive paths, manifest and hash.
- [ ] Resolve required and optional dependencies deterministically.
- [ ] Load only after explicit profile enablement; verify lifecycle order.
- [ ] Reject ABI/API/game-version/capability mismatches before activation.
- [ ] Disable/unload and verify owner subscriptions/resources are cleaned up.
- [ ] Crash one plugin and verify recovery preserves the profile and user data.
- [ ] Display the native-code trust boundary before enabling an untrusted package.

## SDK and GAL adapters

- [ ] Verify adapter callbacks, thread affinity, object lifetime and shutdown.
- [ ] Confirm scripts receive value objects/opaque handles, never native pointers.
- [ ] Verify only discovered capabilities are exposed to each plugin.
- [ ] Test API version negotiation and unsupported-operation behavior.
- [ ] Validate persistence migration, corruption handling and export/recovery.
- [ ] Enable real inventory/world capabilities only after lossless item codecs
      and rollback-safe transactions pass on the exact target.
