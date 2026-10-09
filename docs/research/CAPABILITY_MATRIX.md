# Ma trận khả năng và mức chứng cứ

VERIFIED = mã hoặc tài liệu nguồn đã kiểm tra. UNTESTED = có implementation nhưng
chưa thử target. RESEARCH_REQUIRED = chưa có đủ chứng cứ. UNSUPPORTED = adapter
hiện tại từ chối khả năng. Cột core chỉ phản ánh desktop, không phải Minecraft.

| Khả năng | BedrockForge core | International Bedrock adapter | NetEase docs | Inner Core sources |
|---|---|---|---|---|
| C++ mod lifecycle | IMPLEMENTED; Android sources compile for ARM64, runtime test pending | UNVERIFIED game-process module loading; pinned launch is not evidence of gameplay integration | RESEARCH_REQUIRED native SDK | VERIFIED JNI boundary |
| Python gameplay interface | VERIFIED subset desktop | UNSUPPORTED interpreter chưa nhúng | VERIFIED systems/API | UNSUPPORTED Python; JS/Java patterns |
| Event bus và tick | VERIFIED manual host tick | UNSUPPORTED game tick callback | VERIFIED event methods | VERIFIED Callback |
| Component factory | RESEARCH_REQUIRED contract | UNSUPPORTED | VERIFIED | RESEARCH_REQUIRED compatible modern API |
| Item registry/providers | VERIFIED values/ownership | UNSUPPORTED engine registration | VERIFIED GetLoadItems/basic info | VERIFIED examples |
| Mod lifecycle/profile management | ABI v2 dynamic library discovery and multi-dependency ordering implemented; execution test pending | Host passes enabled-directory libraries to runtime; package import/profile UI not integrated | VERIFIED pack/client/server lifecycle docs | VERIFIED packs/mods separation docs |
| Creative tabs/groups | Registry value contract only | UNSUPPORTED; no gameplay UI shipped | `addCreativeGroup` documented; engine UI details limited | `addToCreativeGroup` wrapper verified; not proof of paginated inventory tabs |
| Authored content-pack item registration | Not part of system runtime | UNSUPPORTED; no gameplay packs shipped | Separate China SDK architecture | Not assessed |
| Block registration | RESEARCH_REQUIRED | UNSUPPORTED | RESEARCH_REQUIRED trong audit này | VERIFIED TileEntity examples |
| Recipe providers/search | VERIFIED logical records | UNSUPPORTED runtime discovery | VERIFIED input/output queries | VERIFIED recipe examples |
| Logical container abstraction | VERIFIED desktop | UNSUPPORTED real game transfer | RESEARCH_REQUIRED container integration details | RESEARCH_REQUIRED transaction semantics |
| Atomic same-owner transfer | VERIFIED desktop | UNSUPPORTED cross-engine commit | RESEARCH_REQUIRED atomicity | RESEARCH_REQUIRED atomicity |
| Journal persistence/recovery | VERIFIED desktop | UNTESTED Android filesystem | VERIFIED ExtraData docs | VERIFIED saver source |
| Touch UI page model | VERIFIED layout utility | UNSUPPORTED; no gameplay UI shipped | VERIFIED UI/control docs | VERIFIED UI examples |
| Native game UI | RESEARCH_REQUIRED | UNSUPPORTED | VERIFIED within China Edition | RESEARCH_REQUIRED modern compatibility |
| Client/server transport | RESEARCH_REQUIRED contract | UNSUPPORTED | VERIFIED Notify methods | RESEARCH_REQUIRED audited transport |
| Mod dependencies/version checks | VERIFIED desktop package preflight; native runtime subset | UNSUPPORTED; Android resolver not wired into launcher | RESEARCH_REQUIRED package semantics | RESEARCH_REQUIRED pack semantics |
| Android profiles/backups/import | `.bfmod` import/activation implemented; no profile manager or device verification | Package install path not device-tested | VERIFIED MCStudio workflow docs | VERIFIED pack architecture source |
| Native code sandbox | UNSUPPORTED | UNSUPPORTED | RESEARCH_REQUIRED runtime guarantees | RESEARCH_REQUIRED |

Nguồn và điều kiện reuse: [ECOSYSTEM_AUDIT.md](ECOSYSTEM_AUDIT.md),
[NETEASE_MODSDK_ANALYSIS.md](NETEASE_MODSDK_ANALYSIS.md), [SOURCES.md](SOURCES.md).
Không có phiên bản Minecraft nào được thêm vào allowlist gameplay của project.

Cập nhật system-only: gameplay mod, content pack và UI demo không nằm trong
repository. Android host là prototype; package discovery/import và adapter
gameplay chưa được tích hợp. Không có phiên bản nào trong gameplay allowlist.
