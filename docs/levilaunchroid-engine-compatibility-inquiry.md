# Draft: LeviLauncher engine compatibility inquiry

Do not publish without approval.

Hello LeviLauncher maintainers,

HybridMCPE targets Minecraft Bedrock Android **1.26.52.3 ARM64** and is evaluating LeviLaunchroid as a host. Could you clarify:

1. Does any released LeviLauncher version support Minecraft **1.26.52.3 ARM64**? If so, which release and engine build were validated?
2. Is the production Preloader C++ lifecycle registration (`PLGetModRegistration`) supported on that exact game version?
3. Is there a documented deployment path that uses an official Google Play installation and preserves genuine license and integrity checks, without relying on a `StubStore` response that unconditionally reports a verified license?
4. Does the loader preserve the installed `libminecraftpe.so` byte-for-byte, or copy/transform it? Does version isolation leave the original installation and its world data untouched?

We will only test through a documented, license-compliant path and will not call unverified engine functions.

Thank you.
