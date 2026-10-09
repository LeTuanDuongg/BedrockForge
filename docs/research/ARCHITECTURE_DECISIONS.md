# Quyết định kiến trúc

## ADR 001 — Loader riêng, bootstrap kiểm chứng trước

BedrockForge sở hữu runtime/SDK/mod format. Host nạp runtime vào cùng tiến trình
game rồi runtime nạp DLL/SO độc lập. Tham chiếu lifecycle Preloader, không viết
APK patcher từ đầu khi chưa chứng minh nhu cầu. Bridge hiện là prototype Android
loading; chưa có APK standalone. Tách việc nạp native khỏi việc có gameplay API.

## ADR 002 — Core độc lập engine và GAL v1

Flow: Android Host → Native Runtime → Game Abstraction Layer (adapter theo
version/ABI/library hash) → engine. Mod → C ABI hoặc scripting facade → core/GAL.
Adapter phải validate fingerprint trước activation và trả capability cụ thể;
fallback hiện tại trả rỗng. Không dùng offsets, struct Bedrock hay native pointers
làm public API. Không có adapter gameplay VERIFIED để bật ở release 0.1.

## ADR 003 — Hai giao diện developer

SDK C v1 là function table có size/version và structs giá trị có giới hạn. C++
wrapper bổ sung RAII, không xuất C++ ABI vào mod. Python 3 reference binding gọi
native core qua C exports, facade chỉ cung cấp dictionaries và integer handles.
Runtime scripting Android là milestone tiếp theo, không giả mạo CPython nhúng.
Tham khảo NetEase systems/components, Inner Core callbacks/containers, Forge và
Fabric registries/events; không copy implementation engine của chúng.

## ADR 004 — Authority và giao dịch inventory

Store theo owner và isolated world root, mọi mutation theo revision. Candidate
state được commit durable thành một record chứa tất cả container cùng owner
trước khi đổi state RAM. Transfer cùng store giữ nguyên tổng count. Incomplete
tail bỏ qua khi restart, complete record lỗi checksum từ chối và yêu cầu quarantine.
Mất provider hoặc metadata không hỗ trợ không được xóa item để tiếp tục load.
Write kết quả không chắc chắn khóa store tới restart.

Deposit/withdraw là logical services; không có quyền tạo game item. Cross-owner,
cross-process, cross-world, cross-engine transactions UNSUPPORTED. Chưa có atomic
commit với vanilla inventory nên không bật ingest/extract thật. Ngoài giới hạn
16MiB journal cần migration/compaction offline có kiểm chứng; không âm thầm cắt dữ liệu.

## ADR 005 — Provider catalog và recipe

Namespace thuộc mod, duplicate bị từ chối. Recipe providers dùng registry chung,
output/ingredients là value records. Browser query registry hiện tại để thấy
provider đăng ký sau nó. Không mặc định full vanilla catalog. Không đóng gói
texture proprietary; icon provider phải chứng minh quyền sử dụng và adapter.

## ADR 006 — Host và compatibility

Manifest framework tách khỏi launcher manifest. Package tooling có semantic
release versions, exact Minecraft versions, dependencies, optional deps, ABI và
capabilities. Empty version list = không có game version verified, không phải
wildcard. Loading smoke package được gắn rõ label và không activation gameplay.
Native descriptor v1 mới hỗ trợ một dependency ID/API integer; full manifest
preflight chưa nối vào bootstrap prototype. Không gọi subset này dependency
manager production hoàn chỉnh.

## ADR 007 — Cleanup và lỗi

Host thread duy nhất, lifecycle reverse shutdown, owner event cleanup, context
cookie sống tới runtime destruction, C boundary chuyển exception thành status.
Safe mode skip gameplay libraries, không xoá dữ liệu. Native plugins không sandbox.
Crash diagnostics hiện là logs/status, chưa có native crash collector.
