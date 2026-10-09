# Kiến trúc BedrockForge rút ra từ Inner Core, Horizon và MCPE China

Tài liệu này biến các mẫu đã nghiên cứu thành quyết định thiết kế cho loader của chúng ta. VERIFIED nói về nguồn/API đã khảo sát, không có nghĩa BedrockForge đã hoàn thành hoặc tương thích với Minecraft mới.

## Ba hệ thống giải ba bài toán khác nhau

| Hệ thống | Quan sát từ nguồn | Bài học cho BedrockForge |
|---|---|---|
| Horizon | Launcher chạy engine pack; pack có thư mục và môi trường native; modpack/profile quản lý world, cấu hình, cài đặt và cập nhật | Host quản lý profile, pack, backup, log, update và target game. Quản trị pack không trộn vào gameplay API |
| Inner Core | Engine API cho mods JS/Java và native wrappers; item/Creative group, callbacks, container/UI, recipes, world saves. Toolchain có TS declarations và build JS/TS/Java/C++ | API ổn định với semantics rõ, có binding nhiều ngôn ngữ. Core value contracts đi qua adapter engine |
| MCPE China / NetEase | Pack đăng ký client/server systems; component factory tạo API theo entity/level; event/notify nối hai phía; inventory/world save chia theo side | Presentation ở client; gameplay state có authority phía server. Script là facade quanh engine component services |

Nguồn chi tiết và commit ghim: [Inner Core/Horizon](../research/INNER_CORE_HORIZON_ANALYSIS.md), [NetEase](../research/NETEASE_MODSDK_ANALYSIS.md), [so sánh](../research/MODDING_FRAMEWORK_COMPARISON.md), [SOURCE_PINS](../research/SOURCE_PINS.json).

## Cấu trúc sản phẩm cần xây

```text
BedrockForge Host
  profiles / packs / backups / logs / updates / target selection
  Mod Manager and compatibility preflight
        │ versioned manifest + lifecycle + capabilities
Runtime
  loader / dependency DAG / events / persistence / script scheduler
  stable C ABI ───── high-level scripting API
        │ copied values and opaque generation handles
Versioned Game Abstraction Layer
  items / blocks / recipes / inventory / UI / world / client-server transport
        │ exact-version adapter selected by version + ABI + library hash
Minecraft engine and authored content packs
```

Host, runtime và engine adapter là ba thành phần riêng. Host chọn profile; runtime kiểm tra và nạp mod theo dependency order; GAL chỉ công bố capability sau khi adapter vượt kiểm chứng đúng target. Content packs có thể thêm assets/items qua cơ chế game hỗ trợ; chúng không tự tạo native callbacks hay giao dịch inventory vanilla.

## Hợp đồng mod và vòng đời

Mỗi package khai báo `id`, `version`, `framework_api`, game target, dependencies, entry point, side (`common/client/server`), assets và capabilities. Trước khi gọi mod, resolver phát hiện dependency cycle, duplicate IDs, API/version mismatch và capability thiếu. Lỗi preflight chặn load an toàn.

Vòng đời thống nhất: `discover → validate → resolve → create scope → register → world attach → run → quiesce → unregister → close`. Mỗi đăng ký sở hữu bởi một owner scope; unload dọn listener, UI và registry entries của owner. Save namespace theo mod/profile/world có schema version và migration. Lỗi mod được cách ly và ghi theo ID; không unwind qua engine callback.

Native C++ dùng C ABI và value structs, được xem là trusted code có quyền process. C++ helper bọc ABI. Script dùng cùng operations qua ID/handle và dữ liệu đã validate, có giới hạn event depth, payload, thời gian và bộ nhớ. Không expose native/game pointers, JNI hay arbitrary symbol lookup cho script.

## Hợp đồng gameplay

- **Registry:** namespaced IDs, localization keys, staging trước world start, duplicate detection và registry generation. Item/recipe provider khai báo version, scope và completeness.
- **Creative tabs:** đăng ký tab ID, icon item ID và danh sách item IDs tường minh. Trang chỉ là presentation. Click giữ stable item identity rồi gọi `game.creative_grant` đã kiểm chứng. Tên dịch, texture path và engine collection index không phải item ID.
- **Events:** event value gồm side, world/entity opaque handles, tick/sequence và cancellation policy. Snapshot dispatch, reentrancy guard, owner cleanup.
- **Components:** APIs theo entity/world trả snapshot hoặc mutate intents. Generation handles hết hạn theo entity/world lifecycle. Không expose engine object/pointer.
- **Inventory:** server/backend làm chủ state. UI gửi `container_id`, slot intent, expected revision và transaction ID. Backend validate codec/count/permission rồi commit nguyên giao dịch; ID lặp được xử lý idempotent. Giao dịch vanilla cần codec roundtrip và crash/recovery tests.
- **Custom UI:** declarative controls/layout, typed callbacks, view token hết hạn khi đóng screen/world. UI gửi intent, không tự sửa item state.
- **Persistence:** namespace theo mod/profile/world, schema và migration. Quy định durability, partial/corrupt journal, backup và recovery.
- **Client/server:** channels có authenticated sender, bounded payload, sequence, validation và lỗi phản hồi. Multiplayer support chỉ công bố khi transport và quyền đã thử thật.
- **Recipes/browser:** typed records gồm recipe type, shape, tags, alternatives/conditions, category và provider provenance. Browser hiển thị giới hạn của mỗi provider; assets APK không phải live world registry.

## Các giai đoạn thực hiện

### P0 — Loader quản lý mod thật

1. Thay danh sách demo cứng trong Android bootstrap bằng manifest discovery từ profile; preflight hash/ABI, dependencies và capabilities trước launch.
2. Nối `ModContext`/owner scopes xuyên lifecycle, events, registry, UI và storage; cleanup, crash report, backup/restore profile.
3. Khai báo common/client/server side và message boundary. Single-process hiện tại không được tính là multiplayer.
4. Duy trì exact version/hash target. Không claim hỗ trợ mọi phiên bản nếu thiếu adapter và thiết bị test tương ứng.

### P1 — Gameplay API

1. Hoàn thiện GAL/capability handshake cho item/Creative grant, event, inventory, UI, persistence và recipe.
2. Hoàn thiện `CreativeTabRegistry` cùng native/provider mapping ổn định. Thiếu resolver thì báo unsupported.
3. Hoàn thiện container API theo thứ tự: block/interaction thật → container identity → UI → codec vanilla → atomic transfer → save/reload/crash recovery.
4. Kết nối registry consumers vào provider có provenance và recipe representation đầy đủ; đánh dấu loại engine không trả.

### P2 — Script và phân phối

1. Chọn interpreter Android sau benchmark ARM64, footprint, startup, lifecycle, licenses và limits. Python desktop facade không bắt buộc Android cũng dùng CPython; Rhino cũ cũng chưa mặc định được chọn.
2. Cho script cùng API semantics sau khi C ABI/GAL ổn định; chỉ expose typed values và opaque handles.
3. Xây manager cập nhật atomically theo profile với hashes/signatures, rollback và lockfile. Native `.so` khác target không tự động tương thích.

## Trạng thái BedrockForge

Đã có C ABI/core, desktop plugin loading, logical storage journal và Android host thử nghiệm. Còn thiếu Android mod discovery/profile resolver production, complete dependency manager, Android script interpreter, native game registry/inventory adapters, stable Creative-tab mapping và multiplayer transport. Gameplay mod không thuộc repository nền tảng. Đây là đường phát triển, không phải danh sách tính năng đã xong.

Không dùng lại NetEase/Inner Core proprietary runtime. Không kết luận trùng tên API nghĩa là cùng semantic; Inner Core native bridge thuộc engine pack của họ, China components chỉ thuộc China SDK, Horizon environment thuộc pack. Không gọi native mod là sandbox, không lẫn Creative group với inventory tab, không suy diễn native compatibility từ tên symbol.
