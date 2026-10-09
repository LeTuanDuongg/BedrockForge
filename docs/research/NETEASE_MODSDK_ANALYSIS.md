# Kiến trúc Minecraft China Edition Mod SDK

Ngày 09/10/2026. [Cổng chính thức](https://mc.163.com/dev/) được truy cập nhưng công
cụ web không lấy được nội dung; dùng kho tutorial công khai và mirror đã clone
để kiểm tra. Các link immutable và commit nằm trong [SOURCES.md](SOURCES.md).
VERIFIED ở đây là verified documentation/source, mọi behavior mobile UNTESTED.

## Biên scripting

`modMain.py` chứa lớp gắn `Mod.Binding`, các hàm `Mod.InitServer`/`Mod.InitClient`
đăng ký system bằng `serverApi.RegisterSystem`/`clientApi.RegisterSystem`.
System kế thừa lớp trả về từ `GetServerSystemCls` hoặc `GetClientSystemCls`.
Đây là điểm mở rộng do runtime China Edition cung cấp, không phải module Python
có thể import bằng CPython thông thường. Nguồn: mirror
`docs/wiki/modsdk/modsdk-intro.md` và `接口/通用/System.md`.

Tutorial `mcguide/20-玩法开发/13-模组SDK编程/2-Python脚本开发/0-脚本开发入门.md`
ghi rõ Python 2 thay vì Python 3. Không suy ra mọi release China Edition vẫn dùng
cùng interpreter; cần kiểm tra phiên bản SDK/device cụ thể. BedrockForge chọn
Python 3 reference SDK trên desktop, chưa chọn interpreter nhúng trên Android.

`GetEngineCompFactory()` tạo component theo entity/level, ví dụ `CreateItem`
và `CreateRecipe`. Public values là entity IDs, dictionaries, tuples và scalar.
`nuoyanlib/core/server/comp.py` import `mod.server.extraServerApi` và gói component
factory. Wrapper không tự implement Bedrock engine.

## Client và server

Client system phụ trách presentation, custom screens, input, local effects và
gửi yêu cầu. Server system xử lý state gameplay có thẩm quyền: tồn kho, world,
entity và lưu dữ liệu. Phân quyền này là thiết kế BedrockForge rút ra từ API
side annotations; không tuyên bố mọi NetEase API đều đảm bảo server authority.

`ListenForEvent`, `UnListenForEvent`, engine namespace/system name và broadcast
được tài liệu hóa. `NotifyToServer`, `NotifyToClient`, `NotifyToMultiClients`
đưa event payload giữa hai phía. Mã `nuoyanlib/common/communicate.py` là ví dụ
RPC/event wrapper. Không có bằng chứng transport này là giao thức công khai
có thể sử dụng trên Bedrock quốc tế. Nguồn: `接口/通用/事件.md`.

## Container và inventory

`接口/方块/容器.md` tài liệu hóa `GetContainerItem(pos, slotPos, dimensionId,
getUserData=False)`, `GetContainerSize` và `SpawnItemToContainer`. Getter container
được đánh dấu server. Danh sách hỗ trợ gồm chest, trapped chest, shulker, hopper,
barrel, dropper và dispenser; Ender Chest dùng API riêng. Item data là dictionary.
Các API lấy item/size không chứng minh custom container 81 hoặc 108 native slots.

Client có API custom UI (`RegisterUI`, `CreateUI`, ScreenNode, controls). Item UI
và engine storage là hai biên riêng. Nuoyanlib có NyUI wrapper và README nhắc
container UI framework, nhưng README cảnh báo đang development/testing. Touch
button callback up/down/cancel/move xuất hiện tại `client/ui/nyc/button.py`.
Chưa xác minh container framework đó bảo đảm transaction atomicity hoặc hỗ trợ
mọi tier, nên các điểm đó RESEARCH_REQUIRED.

Pattern áp dụng cho framework container APIs: view chỉ giữ container ID, page, revision và
slot values; gửi intent với expected revision tới backend authoritative. Không
gán item vào vanilla slot dựa trên dữ liệu UI. Tạo logical 54/81/108 slot, trang
3 hàng với target tối thiểu 48dp. UI không tự mint hoặc tiêu thụ game items.

## Items và recipes

`接口/物品.md` có `GetLoadItems(flag=True)` ở server: trả loaded IDs, flag False
chỉ creative entries. `GetItemBasicInfo`, name/texture và lookup APIs có side
annotations. Đây là documented runtime discovery của China Edition, không
phải bằng chứng international Bedrock có cùng registry export.

`接口/世界/配方.md` có `GetRecipeByRecipeId`, `GetRecipesByInput`,
`GetRecipesByResult`, `AddRecipe` và remove. Query theo input/output có client
và server variants, filter/tag/aux/limit phụ thuộc từng API. Không coi đây là
API enumerate mọi loại recipe của mọi provider.

Framework registry/provider contracts dùng item IDs và recipe value records.
Provider phải khai báo scope, version và completeness. Không có vanilla snapshot
nào được xác minh trong repository; fixture kiểm thử chỉ kiểm tra contract và
không được đóng gói như recipe vanilla.

## Persistence và mobile testing

`接口/世界/自定义数据.md` có Get/Set/Clean/SaveExtraData; phải đọc scope, save flag
và lifecycle cụ thể thay vì giả định mọi Set lập tức durable. BedrockForge dùng
mod/world-owned journal riêng, không tái sử dụng file runtime NetEase.

Tutorial `modsdk-intro.md` mô tả MCStudio tạo Addons, script trong behavior pack,
resource pack riêng, chọn development test, game version/test world và xem log.
Workflow mobile cần test đúng China Edition SDK, chuyển pack theo công cụ developer
được hỗ trợ và thử input thật, screen sizes, reconnect và save/reload trên máy.
Tutorial chính thức `mcguide/27-手机网络游戏/课程9：服务器上线/第2节：PE测试.md`
ghi riêng network-game workflow: gửi game review trước khi PE test client truy
cập được, rồi lấy QR tải client trong MCStudio → quản lý → test launcher download.
Không áp dụng điều kiện review này cho mọi loại offline addon khi chưa xác minh.
Phiên này không có MCStudio/account/device; chưa chạy test hoặc publish nội dung.
BedrockForge cần checklist riêng cho Google Play Bedrock và loader, không dùng
MCStudio như phép thử compatibility với international engine.

## Điều chưa được công bố

VERIFIED: public Python modules, systems, factory/component APIs, event messaging,
UI/value dictionaries. RESEARCH_REQUIRED: implementation C++ của engine binding,
symbol/offset map, threading guarantees, Python/native marshalling internals,
ABI và authorization của transport. Trong nguồn đã kiểm tra không có native
engine bridge đầy đủ hoặc license cho proprietary runtime.

Hypothesis: Python calls đi qua binding native của engine đã tích hợp, vì component
API tác động gameplay. Đây là suy luận kiến trúc, không xác định được dùng CPython
extension, JNI, IPC hay cơ chế khác. Không tạo tên DLL/symbol/offset từ suy luận.
Direct reuse proprietary runtime trong international Bedrock: UNSUPPORTED.
