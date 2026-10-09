# Inner Core và Horizon: áp dụng cho BedrockForge

Ngày đối chiếu: 2026-10-09. Đây là nghiên cứu kiến trúc và API; không phải xác nhận đã chạy Inner Core/Horizon trên Minecraft 1.26.30.5.

## Quan hệ giữa hai thành phần

Horizon là launcher/môi trường chạy các engine pack. Inner Core là engine modding cung cấp API gameplay trong pack. Tài liệu phân biệt ứng dụng Inner Core độc lập cũ với Inner Core chạy trong Horizon, cùng các pack chính, Test và Legacy. Vì vậy có thể gọi trải nghiệm Horizon là thế hệ tiếp theo, nhưng không nên coi hai tên là hai bản của cùng một thư viện gameplay.

Nguồn: [trang hệ sinh thái](https://inner-core.org/) và [hướng dẫn cài engine](https://nernar.github.io/docs/getting-started/installing-pack). Các mô tả marketing về hiệu năng/đồng bộ không được coi là kết quả kiểm thử của BedrockForge.

## Nguồn và mức tin cậy

- Nguồn developer trực tiếp: [innercore-mod-toolchain](https://github.com/zheka2304/innercore-mod-toolchain/tree/1a7bf2e2e58960f7fe794b2e53c4ebcf6c87bf13), đặc biệt `toolchain/toolchain/declarations/core-engine.d.ts` và README. Toolchain có đường build JS/TS, Java và C++; việc có TypeScript không có nghĩa engine chạy TypeScript trực tiếp.
- Tài liệu hệ sinh thái: hướng dẫn cài engine ở trên, được liên kết từ trang Inner Core.
- Nguồn kiểm tra ranh giới Java/JNI: [CheatBoss/InnerCore-horizon-sources](https://github.com/CheatBoss/InnerCore-horizon-sources/tree/84b72431e7cb702b7d929c61c340a8db07d6ece1). Đây là snapshot bên thứ ba có dấu hiệu decompile, không xem là native source chính thức hay giấy phép để sao chép. Chỉ dùng làm chứng cứ giới hạn về cấu trúc wrapper; không nhập mã vào sản phẩm.
- Không tải/cài APK Horizon, không thay thế game trên điện thoại, không chụp màn hình trong lượt nghiên cứu này.

## Các ranh giới đã quan sát

| Tầng | Chứng cứ | Điều chưa chứng minh |
|---|---|---|
| Launcher/pack | `HorizonLibrary.java` nạp thư viện nền; `launcher/pack/Pack.java` có load resources, shared objects, environment libraries và launch activity | Không chứng minh hoạt động trên Android/engine hiện tại của chúng ta |
| Script | `mod/executable/Compiler.java` sử dụng Rhino/JavaScript; toolchain khai báo API bằng TypeScript | Không phải Python runtime; không tự động tương thích mod BedrockForge |
| Item | `Item.createItem`, `addToCreative`, `addCreativeGroup`, `addToCreativeGroup` trong declarations | Không công bố offset/calling convention cho libminecraftpe.so 1.26.30.5 |
| Native boundary | `api/NativeItem.java`: wrapper tạo item gọi native constructor; nhóm Creative chuyển các ID xuống native method | Thân hàm C++ và cách hook registry không hiện ra qua khai báo JNI |
| UI | `api/mod/ui/window/UITabbedWindow.java` có nội dung, chỉ mục tab và listener | Cửa sổ tab của mod không đồng nghĩa tab Creative tích hợp vào inventory vanilla |
| Container/network | `apparatus/api/container/ItemContainer.java` có slot, transfer policy, transaction lock, sendChanges; Network và NetworkEntity phân lớp đồng bộ | Lock trong process không tự chứng minh crash atomicity hoặc chống duplication qua mạng |
| Recipes/persistence | `WorkbenchRecipeRegistry.java`, `WorldDataSaver.java`, cùng ví dụ RedPowerPE đã pin trong audit | Không thể dùng trực tiếp để khám phá registry ở engine hiện tại |

Các đường dẫn Java trong bảng nằm dưới `com/zhekasmirnov` tại snapshot bên thứ ba nêu trên. Những quan sát này không được nâng thành kết luận về mọi bản Inner Core.

## Điểm quyết định đối với tab Ruby

API `Item.addCreativeGroup(name, displayedName, ids)` mô tả một nhóm bằng danh sách ID. Nó không đủ chứng cứ để khẳng định có API tạo hàng tab Creative phân trang giống Forge. `UI.TabbedWindow` cũng là API cửa sổ có tab riêng; cần phân biệt rõ ba khái niệm: nhóm Creative, tab của cửa sổ mod và tab của inventory Minecraft.

BedrockForge hiện lọc collection Equipment bằng hover label để dựng tab Ruby. Đây chỉ là prototype: phụ thuộc bản dịch, có thể trùng tên, chưa có ánh xạ chỉ mục gọn, và bản sửa cuối chưa được nghiệm thu trên thiết bị. Nghiên cứu Inner Core không làm prototype này trở thành một API tab hoàn chỉnh.

Thiết kế thay thế cần một `CreativeTabRegistry` do framework sở hữu:

1. Mỗi tab có `mod_id:tab_id`, khóa tên dịch, icon item ID, thứ tự và danh sách item ID rõ ràng.
2. Resolver chuyển item ID ổn định sang handle engine đúng phiên bản; không dùng tên hiển thị hoặc số thứ tự Equipment làm danh tính.
3. View tính trang từ danh sách tab đã đăng ký, tạo danh sách slot liên tục và giữ ánh xạ slot → item handle kèm generation của registry.
4. Chọn item gọi thao tác Creative đã xác minh, kiểm tra chế độ chơi/quyền và stack codec; đóng world hoặc reload registry phải vô hiệu handle cũ.
5. Thiếu native resolver/transaction capability thì báo unsupported. Một icon xuất hiện không được tính là lấy item thành công.

Đây là quyết định kiến trúc mới, chưa phải implementation native đã hoàn thành. C++ SDK và scripting facade phải cùng sử dụng registry; script chỉ nhận ID/opaque token, không nhận pointer.

## Áp dụng cho các mod còn lại

- Container APIs: học cách tách storage state, UI và đồng bộ. Dùng world/container identity, server authority, revision và transaction ID; giao dịch với inventory vanilla chỉ bật khi codec đầy đủ và commit/rollback được kiểm chứng. `runTransaction` không thay thế journal/crash recovery.
- Item & Recipe Browser: học registry/provider và tích hợp recipe viewer từ RedPowerPE. Lưu recipe type, shape, alternatives và provider provenance; không suy ra recipe live từ texture hoặc chỉ một danh sách tên.
- Loader: học engine pack/profile có version và native environment riêng. BedrockForge tiếp tục dùng engine do người dùng sở hữu, kiểm tra hash/ABI, và không phân phối lại game binary.
- Script SDK: học API cấp cao có lifecycle rõ ràng từ JS/Java/JNI. Giữ quyết định runtime của BedrockForge độc lập; việc tham khảo Rhino không tự động quyết định đổi Python sang JS hay nhúng các wrapper cũ.

## Kết luận kiểm chứng

Điểm đáng học là đăng ký dữ liệu gameplay qua API ổn định, liên kết native theo engine pack, và tách UI khỏi authority của inventory. Không có chứng cứ trong các nguồn đã đọc rằng thư viện native cũ có thể nạp vào Minecraft 1.26.30.5 và cung cấp ngay các API đó. Công việc còn thiếu của BedrockForge là adapter thực hiện các contract này và kiểm thử giao dịch thật, không chỉ thêm tên hàm giống Inner Core.
