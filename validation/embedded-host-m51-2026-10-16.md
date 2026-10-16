# Imported-engine host milestone — Samsung M51, 2026-10-16

## Verified scope

Minecraft 1.26.50.4 ARM64 reaches its real menu and a local world through
`dev.hybridmcpe.launcher/.HostedMinecraftActivity`. The existing HybridMCPE
runtime and a native Mod ABI 1 diagnostic module execute in the same game
process. This is a new imported-engine host route. The previous modified-APK
route remains NO-GO, and its unexplained exit has not been diagnosed by this work.

The user approved installing/updating/testing HybridMCPE on the M51. Only the
HybridMCPE app was updated. No original Minecraft installation, account or world
was replaced. The host's own test world was saved through Minecraft's Save & Quit.

## Identities

- Device: RF8R31WEK5A, SM-M515F, Android 12, AArch64.
- Source package: com.mojang.minecraftpe, version 1.26.50.4, versionCode 972605004.
- Source APK SHA-256 before and after testing:
  `7b1ef882ecfec9c41155488e38f69654b95559ab69836d15573cfe2d29232039`.
- Source signer SHA-256:
  `b369922ee3e720f359b9eec13af554dc87a0ec09f3ac49e7e123bc94fb985890`.
- Unmodified engine SHA-256:
  `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- Engine size: 328,419,680 bytes, AArch64.
- Tested host APK SHA-256, also matched against the installed host base APK:
  `18eeae5b292667fe382f2c48fc0d9fa739cda1ad079cbf0a5f6e994ed8473a5c`.
- Host signing identity: `CN=HybridMCPE Development`, certificate SHA-256
  `0f3b814d6cf8d489d0fd936a7ddf3d020841f1845b1e25c94a0361d623dcd9ac`.
- Runtime SHA-256:
  `dc0184985a4d4c64c59f30fd083632f49dc43b24b478ba104919024a9de2b618`.
- Diagnostic module SHA-256:
  `315fcaa7df0d81591ae720525e719ad5203af7c4507d7e2a5b1b590121abb369`.

The source APK is user supplied. Its signature verifies, but publisher identity,
distribution provenance and official status remain unverified. No signature or
license response was replaced. Original game code logged a cached LICENSED
response; that observation is not proof of official publication or independently
verified license entitlement. No personal account was signed in.

## Implemented path

1. Copy installed base/split APKs or user-selected document APKs into a private
   transactional repository. No source installation or Play installer path is required.
2. Verify each APK cryptographically using pinned Android SDK apksig, detect
   version/versionCode, match split package/version/signers and declared dependencies.
3. Extract and fingerprint ARM64 libraries; retain the unchanged APKs as DEX,
   resources and asset sources. Imports have separate version/data directories.
4. AppComponentFactory creates the game process classloader. The host subclass
   and its D8 companion classes resolve alongside the real imported MainActivity.
   No Minecraft implementation is shipped in the host APK.
5. Public ResourcesLoader APIs supply imported APK assets/resources. HostContext
   directs normal Context storage APIs into the version's private data directory.
   This is path separation, not a security boundary against malicious imported code
   in the same host UID. The original game UID remains separate and inaccessible.
6. Initialize the original Firebase/Play Games SDKs through their public entry
   points, with original resources and component registrar metadata. Store,
   authentication and integrity code is loaded from the source APK unchanged.
7. After a menu-only host test, diagnostic mode calls the existing native runtime
   through JNI. The runtime verifies the real mapped engine's full fingerprint,
   then loads the bundled Mod ABI 1 diagnostic module. No gameplay function is called.

The pinned Preloader adapter remains available and unchanged in ABI. This actual
host test uses the JNI bootstrap, not a live LeviLauncher/Preloader deployment.

## Device results

- Menu/world baseline PID 5311 started at 15:25:16, reached Play menu and a
  Creative world, survived recents switching, remained alive past 15:30:52
  (more than five minutes), then successfully saved and quit the world.
- Final diagnostic host PID 10830 started at 15:34:34. At 15:34:54 the native
  diagnostic logged `diagnostic_mod=start abi=1 pid=10830 capabilities=0`.
- Native runtime reported exact engine profile/hash, AArch64,
  `layout_validated=0`, `capabilities=0`, `diagnostic_load=0` in PID 10830.
- Recents transition at 15:35:45 logged native `stop` and `diagnostic_suspend=0`.
  Return at 15:35:48 logged native `start` and `diagnostic_resume=0`, same PID.
- A real world was visible with the diagnostic host at 15:39:21. PID 10830
  remained alive and foreground at 15:40:35, more than five minutes after native
  module startup. No fatal exception, JNI abort, native fatal signal or explicit
  exit appeared in the captured PID log for this run.
- This is one successful cold launch of the final diagnostic build. Three
  consecutive cold launches and long-session stress have not been established.

Observed host defects fixed during development: Android 12 lacks the newer
classloader locking API; classes in different classloaders do not share package
access; D8-generated companion classes must resolve with the host Activity;
omitted Firebase/Play Games startup caused a verified JNI abort. The fixes retain
original termination and protection behavior.

Local full logs/screenshots: `build/evidence/embedded-host-*` (ignored; not
redistributed). The source game APK and engine remain local ignored artifacts.

## Checks and remaining limits

- ARM64 build with NDK 28.2.13676358, signed host APK verification: PASS.
- Actual-APK importer tests: signature/signer/version, extraction, duplicate
  imports preserving data, atomic rollback, changed-library detection, source
  preservation: PASS.
- Signed split import tests: reversed selection order, missing base/split,
  mismatched signer/version, unknown-profile gating: PASS. Fixtures use a copy of
  the local real engine, are never executed/installed, and are not distributed.
- Host APK DEX boundary test: no com.mojang implementation/compile-only API,
  no Minecraft engine, apksig NOTICE present: PASS.
- Existing 1.26.52.3 profiles/research retained. This host's startup compatibility
  is verified only for the exact supplied 1.26.50.4 engine, not every APK/version.
- Unknown engines receive fingerprint-only import records and remain unavailable
  to the runtime. No wildcard fingerprints or borrowed offsets/layouts are used.
- Gameplay capabilities remain zero. Iron Chest, block registration, engine
  transactions, custom UI and world-save integration are not yet functional.
- The build loads a bundled diagnostic module. General third-party module import,
  compatibility discovery, authentication flows and version-switch stress remain
  unfinished. A native host milestone is not a claim of a finished modding platform.

Public architecture references consulted, without copying their store stubs:

- https://github.com/LiteLDev/LeviLaunchroid/blob/v1.5.27/app/src/main/java/org/levimc/launcher/core/minecraft/GamePackageManager.kt
- https://github.com/LiteLDev/LeviLaunchroid/blob/v1.5.27/minecraft/src/main/java/com/mojang/minecraftpe/MainActivity.java

Focused APK evidence: REA `ev_1c75d3313b933d242ccddd5f64d5cbebc7d10e7708a1804c8c31dac4593004f2`
confirms NotificationListenerService exists in the actual source APK;
`ev_82f35c0ade844035510e819c4b022412862d87c7882bc243b870989348143e8a`
records original MainActivity.onCreate. Neither static observation substitutes for
the device results above.
