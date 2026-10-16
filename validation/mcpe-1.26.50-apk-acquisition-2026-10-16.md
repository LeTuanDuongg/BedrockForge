# Minecraft 1.26.50 APK acquisition check — 2026-10-16

Historical acquisition/security snapshot, before the user authorized execution of
the supplied APK and the independent imported-engine host was implemented.
Later device execution evidence is recorded separately in
`embedded-host-m51-2026-10-16.md`. Publisher provenance remains unresolved.

## Acquired artifact

- Copied from the M51 Download directory: `minecraft-26-50-arm64-v8a.apk` (363,050,262 bytes).
- Local copy: `build/target-apk-analysis/minecraft-26-50-arm64-v8a.apk`.
- APK SHA-256: `7b1ef882ecfec9c41155488e38f69654b95559ab69836d15573cfe2d29232039`.
- Manifest/badging: package `com.mojang.minecraftpe`, version `1.26.50.4`, versionCode `972605004`, min SDK 29, target SDK 36, native ABI `arm64-v8a`.
- The APK contains `lib/arm64-v8a/libminecraftpe.so` (328,419,680 bytes) and extensive assets. Its manifest has no split attribute; the engine is in this APK. No separate APK split was identified.
- Extracted engine SHA-256: `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.
- APK v3 signature verifies cryptographically. Signer certificate SHA-256: `b369922ee3e720f359b9eec13af554dc87a0ec09f3ac49e7e123bc94fb985890`.

## Provenance and device state

The M51 Package Manager reports the installed package as version `1.26.50.4`, versionCode `972605004`, installed by `com.google.android.packageinstaller` at `2026-10-16 13:58:13`. A read-only pull of its installed `base.apk` is byte-for-byte identical to the Downloads APK (same SHA-256). The Download Manager source URI could not be retrieved. Package Installer as installer does not identify the original website or establish publisher authenticity.

The available 1.26.52.3 baseline APK is signed with certificate SHA-256 `a40da80a59d170caa950cf15c18c454d47a39b26989d8b640ecd745ba71bf5dc`; the 1.26.50.4 signer differs. This proves the signer identities differ, but alone does not prove why. Because the acquisition source and publisher identity are unverified, do not execute this 1.26.50.4 APK or use it for in-game tests. No app was launched or changed during this check. The existing device installation was already present when this task began.

## Target profile

`targets/bedrock-1.26.50.4-arm64.json` records only the exact ELF fingerprint and AArch64 architecture. RTTI and layouts are empty; the profile cannot substantiate any engine contract. The file is a quarantined binary identity record, not a validated gameplay target. Existing 1.26.52.3 files and records were left intact.

## LeviLauncher host evidence and decision

LeviLaunchroid [v1.5.24 release notes](https://github.com/LiteLDev/LeviLaunchroid/releases/tag/v1.5.24) state “Added inbuilt mod support for Minecraft 1.26.50.” They do not identify patch `1.26.50.4` specifically. Its [v1.5.24 StoreFactory](https://github.com/LiteLDev/LeviLaunchroid/blob/v1.5.24/minecraft/src/main/java/com/mojang/minecraftpe/store/StoreFactory.java) routes `createGooglePlayStore` to `StubStore`; [StubStore](https://github.com/LiteLDev/LeviLaunchroid/blob/v1.5.24/minecraft/src/main/java/com/mojang/minecraftpe/store/StubStore.java) reports `hasVerifiedLicense()` as `true`. That is a license-verification bypass, so this LeviLauncher route is not usable under the project constraints. No LeviLauncher installation or launch test was performed.

**Decision: NO-GO for real-host testing now.** The APK's provenance/signature relationship is unresolved, and the examined LeviLauncher host implementation unconditionally claims verified licensing. Next prerequisite: obtain the exact 1.26.50.x ARM64 package from a publisher-authorized source whose signer is verified, and an authorized host path that preserves license checks. Then request approval before any device installation or launch test.

## Verification boundary

Confirmed: copied APK identity, package metadata, v3 signature validity, signer fingerprint, embedded ARM64 engine and its SHA-256, and equality between the downloaded and installed base APK. Not established: download origin, authenticity against a publisher-authorized certificate, startup stability, world entry, or HybridMCPE execution in Minecraft.
