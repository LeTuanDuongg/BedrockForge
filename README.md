# BedrockForge

BedrockForge là dự án xây dựng **mod launcher và nền tảng phát triển mod cho
Minecraft Bedrock Android**. Repository này chỉ chứa hệ thống: native runtime,
SDK C/C++ và scripting, GAL/adapters, quản lý vòng đời, capability, registry,
storage abstraction, persistence, packaging và công cụ build. Không phát hành
gameplay mod mẫu trong repository.

Mục tiêu dài hạn là để cộng đồng dùng SDK/framework của BedrockForge phát triển,
đóng gói, cài đặt và quản lý mod riêng. Android host hiện nhập được gói `.bfmod`
native vào vùng riêng và nạp thư viện khi mở game; profile manager, scripting
runtime Android và adapter gameplay cho engine chưa hoàn chỉnh. Chưa xác minh
mod có thể thay đổi gameplay trong Minecraft. Native plugin có quyền của process
host và không được sandbox.

BedrockForge chỉ sở hữu mã nguồn runtime/launcher/SDK. Không chứa Minecraft APK,
engine libraries hay runtime độc quyền của NetEase; không sửa binary game hoặc
bỏ qua license. Android host dùng bản game người dùng đã cài và kiểm tra một
target cụ thể, không hứa hỗ trợ mọi phiên bản.

## Build và kiểm tra framework trên desktop

Cần compiler C++20, CMake >=3.22 và Ninja.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests/unit -p 'test_*.py' -v
python tests/integration/test_scripting.py C:/absolute/path/build/libbedrockforge.dll
```

Test plugin nằm trong `tests/fixtures` và chỉ dùng để kiểm tra ABI/loading; đó
không phải gameplay mod hoặc gói phân phối. Trên Linux, thay DLL bằng
`build/libbedrockforge.so`.

## Android host prototype

Cần Android NDK `27.2.12479018`, CMake, Ninja, ADB và JDK/Android Studio để build
APK host. Target hiện ghim ARM64 và một phiên bản Minecraft đã được nghiên cứu;
target khác phải bị từ chối cho đến khi được xác minh. Host giữ dữ liệu trong
profile riêng và không đóng gói game binary.

```powershell
./tools/build-android.ps1 -Ndk C:/Android/ndk/27.2.12479018
python tools/build-host.py
```

APK này là launcher prototype: có import gói native cơ bản nhưng chưa có profile
manager hoàn chỉnh, scripting runtime Android hoặc gameplay adapter được xác minh. Tình trạng và giới hạn nằm
trong [báo cáo kiểm chứng](docs/testing/VALIDATION_REPORT.md) và
[ma trận khả năng](docs/research/CAPABILITY_MATRIX.md).

## Tài liệu

- Kiến trúc: [overview](docs/architecture/OVERVIEW.md),
  [Android host](docs/architecture/ANDROID_HOST.md),
  [scripting runtime](docs/architecture/SCRIPTING_RUNTIME.md),
  [game component API/GAL](docs/architecture/GAME_COMPONENT_API.md),
  [native module ABI](docs/architecture/NATIVE_MODULE_ABI.md)
- Gói mod: [package format](docs/development/PACKAGE_FORMAT.md)
- JEI reference: [feature and compatibility analysis](docs/research/JEI_REFERENCE_ANALYSIS.md)
- Nghiên cứu: [NetEase Mod SDK](docs/research/NETEASE_MODSDK_ANALYSIS.md),
  [so sánh hệ sinh thái](docs/research/MODDING_FRAMEWORK_COMPARISON.md),
  [bài học Inner Core/Horizon](docs/architecture/ECOSYSTEM_LESSONS.md)
- Kế hoạch và trạng thái: [capability matrix](docs/research/CAPABILITY_MATRIX.md),
  [feature status](docs/testing/FEATURE_STATUS.md),
  [device checklist](docs/testing/DEVICE_CHECKLIST.md)
