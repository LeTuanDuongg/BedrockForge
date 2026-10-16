# APK verification dependency

The launcher packages only `com.android.apksig` classes from Android SDK Build Tools
36.0.0 `lib/apksigner.jar`, SHA-256
`3716d9311e55d2b0918a2fd9d54ba9e406c5f6abeea700b287f11259bc163dec`.
The build refuses a different artifact. The SDK's complete NOTICE is included in
the launcher at `assets/third-party/apksig-NOTICE.txt` (Apache License 2.0 and
applicable third-party notices).

Upstream: https://android.googlesource.com/platform/tools/apksig/

APK signatures are checked for the current Android API. This proves signed byte
integrity and identifies certificates, not publisher authenticity, safe code or
licensing. No Play installation source is required. The SDK-internal binary XML
parser is deliberately pinned with the verifier; an SDK upgrade requires API
and parser regression review.

`android/compileOnly` contains an Android Activity type declaration for compiling
the host subclass. It is never DEXed or packaged. At runtime the superclass and
all Minecraft implementations come from the unmodified, signature-verified APK.
