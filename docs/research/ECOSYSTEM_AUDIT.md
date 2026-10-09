# Kiểm toán hệ sinh thái BedrockForge Android

Kiểm tra ngày 09/10/2026. Mã nguồn được ghim trong [SOURCES.md](SOURCES.md)
và [SOURCE_PINS.json](SOURCE_PINS.json). Không dự án nào được chạy trên Android
trong phiên này. VERIFIED chỉ xác nhận bằng mã/tài liệu, không xác nhận hành vi
trong game. UNTESTED: có đường triển khai nhưng chưa thử; RESEARCH_REQUIRED:
thiếu bằng chứng; UNSUPPORTED: không có chức năng đó trong adapter hiện tại.

## Quyết định tái sử dụng

Dùng LeviLaunchroid làm host tham chiếu và Preloader làm biên nạp native ban đầu.
BedrockForge vẫn sở hữu runtime, manifest, SDK và gameplay core riêng. Chưa fork
hay sao chép launcher. Không đưa mã, texture hoặc binary upstream vào sản phẩm.
Quyền phân phối Preloader và thư viện GlossHook đi kèm phải được xác minh trước
khi phát hành binary. Nạp được `.so` không chứng minh có API gameplay.

## LeviLaunchroid

- License gốc Apache-2.0: giữ license, thông báo thay đổi và NOTICE nếu áp dụng.
  Module `pojav_controls` có license/NOTICE riêng, cần kiểm tra độc lập trước reuse.
- Java/Gradle host Android, C++/JNI native bridge. Tài liệu compatibility tại commit
  ghim ghi Android 9+, ARM64, yêu cầu bản Minecraft chính thức Google Play và từ
  1.21.80. Đây là baseline của launcher, không phải danh sách phiên bản đã kiểm
  chứng gameplay cho BedrockForge.
- VERIFIED trong nguồn: phiên bản/profile cách ly, import/export world và pack,
  native package `.levipack`, lifecycle và Mod Menu. Nguồn: `docs/guide/compatibility.md`,
  `docs/guide/developer.md`, `examples/full-cpp-mod/manifest.json`.
- Input/touch, HUD và config có biên Preloader; không có bằng chứng rằng các API
  đó cung cấp container inventory, item/block registration hay recipe discovery.
  Các mục này RESEARCH_REQUIRED, không suy ra từ UI overlay.
- Host backup/restore và config thuộc launcher. Persistence dữ liệu gameplay do
  BedrockForge tự chịu trách nhiệm. Compatibility và dependencies của native mod
  phải được thêm vào manifest framework; không thay manifest launcher.
- Commit ghim 09/10/2026 cho thấy hoạt động gần ngày audit, không bảo đảm ổn định.

## preloader-android

- C++20, CMake, JNI; `include/pl/Mod.hpp` định nghĩa `PL_REGISTER_MOD`, entry
  `PLGetModRegistration`, lifecycle load/enable/disable/unload và mod directories.
  `src/pl/PreLoader.cpp` nối Java loader với native manager. VERIFIED trong nguồn.
- `include/pl/ModMenu.hpp` cung cấp menu, callback nút, input và primitive HUD.
  Hook/patch/signature helpers không phải SDK inventory hoặc recipe.
- `CMakeLists.txt` chỉ cho Android, hỗ trợ ARM và ARM64 và dùng thư viện GlossHook
  dựng sẵn. Dependency upstream có tag phiên bản. BedrockForge không dùng offsets.
- Không tìm thấy LICENSE cấp repo tại commit ghim. Không kết luận public repository
  đồng nghĩa được phép phân phối. RESEARCH_REQUIRED về quyền reuse/linking và
  GlossHook; prototype bridge chưa được dựng hoặc phân phối trên Android.
- Inventory, persistence world, item/recipe APIs: RESEARCH_REQUIRED. Mod directories
  và typed config không thay thế giao dịch storage an toàn.
- Commit ghim 30/09/2026. Native loading UNTESTED trong môi trường này.

## innercore-mod-toolchain

- Toolchain Python, TypeScript/JS, hỗ trợ Java và C/C++ components. README ghi
  Python 3.7+, JDK 8, NDK r16b cho native; xuất `.icmod`, build/push qua ADB.
- Đây là công cụ build, không phải implementation của engine inventory. Tách
  compiler PC Python khỏi runtime script của Inner Core. Không gán phiên bản MC
  hiện đại dựa trên ngày cập nhật toolchain.
- Không tìm thấy license gốc rõ ràng. Reuse implementation RESEARCH_REQUIRED;
  chỉ tham khảo cấu trúc project. Chính README cảnh báo overhaul đang diễn ra.
- Item/container/UI/recipe/persistence API thuộc Inner Core, không được chứng
  minh riêng bởi toolchain. MC/Android runtime compatibility cần xác nhận theo pack.
- Commit ghim 05/09/2026; build toolchain trong phiên này UNTESTED.

## Horizon / Inner Core

- Repo công khai tìm được `CheatBoss/InnerCore-horizon-sources` ghim commit
  10/04/2021 có cấu trúc Java và dấu hiệu nguồn decompile; provenance và quyền
  phân phối chưa xác lập. Không coi đây là nguồn chính thức có thể copy.
- `HorizonLibrary.java` xử lý asset native và biên launcher; `NativeAPI.java`
  khai báo JNI native. `Callback.java`, `Container.java`, `WorldDataSaver.java`
  cho thấy callback, UI slot/container và scope lưu dữ liệu.
- Các lớp Java/JS và native bridge cho thấy pattern kiến trúc hữu ích, không
  công bố implementation Bedrock engine đầy đủ. Version compatibility hiện đại
  RESEARCH_REQUIRED. Không tái dùng offsets của các bản MCPE cũ.
- Tài liệu/toolchain và RedPowerPE cho thấy hệ sinh thái gameplay có item, recipe,
  máy và inventory UI. Chưa xác nhận những feature ấy chạy với Bedrock quốc tế
  đang được hỗ trợ bởi Levi. Maintenance của repo tham khảo chỉ tới 2021.

## RedPowerPE

- README mô tả mod chạy trên Inner Core pack trong Horizon; TypeScript và toolchain
  build/push. Không ghi mapping phiên bản Minecraft/Android đáng tin cậy cho
  baseline hiện tại. Compatibility với BedrockForge UNSUPPORTED.
- `src/dev/items/bags.ts`: đăng ký canvas bag, stack limit 1, backpack 27 slot,
  extra data và shaped recipes. `api/MachineRegistry.ts`: TileEntity prototype,
  callback LevelLoaded và `UI.StandartWindow` với inventory. VERIFIED trong nguồn.
- Native loading do Inner Core/Horizon đảm nhiệm; mod này không chứng minh một
  cơ chế native loading độc lập. Integration recipe viewer dùng API mod khác;
  provider/dependency pattern đáng tham khảo.
- Không có LICENSE gốc rõ ràng trong repo đã kiểm tra. README nói texture RedPower
  thuộc Eloraam. Không copy code hoặc artwork. Persistence end-to-end và networking
  cần chạy pack gốc để kiểm chứng, không suy ra từ useExtraData.
- Commit ghim 08/09/2026; acceptance trên thiết bị UNTESTED.

## JEI-Bedrock

- MIT, C++20/CMake, Levi Preloader lifecycle và packaging. Giữ copyright/license
  nếu reuse; hiện không copy code.
- README nói rõ Phase 0 chỉ để chứng minh load/init, **không item registry, recipes,
  UI, Mod Menu, hooks hoặc gameplay**. Vì vậy các feature browser UNSUPPORTED trong
  revision này, không thể dùng như bằng chứng JEI hoạt động trong game.
- Tài liệu hướng build ARM64 Android API 24; baseline đó không thay baseline Android
  9 của launcher hiện tại. Không có báo cáo device test được tái lập trong phiên này.
- Paths/config và lifecycle là foundation, không phải persistence gameplay hoặc
  dependencies hoàn chỉnh. Commit ghim 06/09/2026; load trong môi trường này UNTESTED.

## NetEase Mod SDK, mirrors và nuoyanlib

Phân tích chi tiết trong [NETEASE_MODSDK_ANALYSIS.md](NETEASE_MODSDK_ANALYSIS.md).
NetEase cung cấp scripting/component boundary ở bản China Edition; không có
bằng chứng runtime proprietary đó được cài vào Bedrock quốc tế.
`MCNeteaseDevs/netease-bedrock-wiki` là kho tutorial, EaseCation là mirror API/docs,
không phải native engine implementation. Quyền sao chép tài liệu chưa rõ nên chỉ
tóm tắt và liên kết. `nuoyanlib` BSD-3-Clause là Python wrapper; giữ copyright,
ba điều khoản và disclaimer nếu reuse. Nó vẫn cần NetEase engine APIs.

## Kết quả áp dụng

Tái tạo mô hình extensibility bằng mã mới: event bus, registry/providers, inventory
logic, journal, lifecycle, capability discovery và SDK C/Python. Chưa có native
adapter gameplay VERIFIED. Không có bản APK standalone hoặc bản game sửa đổi.
