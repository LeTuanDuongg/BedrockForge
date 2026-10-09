# Scripting SDK và runtime

## Implementation hiện tại

`sdk/scripting/bedrockforge.py` là Python 3 reference SDK chạy trên desktop,
gọi **native core thật** qua ctypes/C exports ở `native_bridge.cpp`. Scripting
integration test nạp DLL đã build, đăng ký item giữa hai mod namespaces, tạo
108 slots, deposit, phát hiện revision cũ và reload journal. Không dùng mock core.
Python nhận integer host/session/container tokens, dictionaries và dataclass;
không nhận `void*` context hoặc con trỏ Bedrock. Binding nội bộ sử dụng ctypes
để marshal struct, không phải native pointer API cho gameplay script.

Đây là prototype SDK có subset: capabilities, item registration/search,
recipe registration/query, logical inventory và trusted native mod bootstrap.
Chưa có Python event/lifecycle/UI/messaging bindings
đầy đủ, chưa nhúng interpreter Android và chưa có script package loading.
Desktop test UI ở `tools/preview_ui.py` nạp hai demo DLL độc lập; browser view
gọi export query của chính mod, không thay native query bằng browser fixture logic.

## Thiết kế runtime Android (chưa triển khai)

Native Runtime → script scheduler → scripting SDK → GAL/core services.
Mỗi script mod có owner, dependencies, side client/server, quotas và registry
subscription cleanup. Authority, version/capability checks giống C SDK. Value
records được validate trước marshal; không cho lookup symbols, memory reads,
ctypes, JNI hoặc native function addresses qua SDK.

Interpreter selection còn RESEARCH_REQUIRED: cần quyết định Lua hay CPython 3
dựa trên ARM64 footprint, Android build support, lifecycle và limits. Không
copy Python 2 của NetEase chỉ vì API mẫu dùng nó. Reference Python SDK định nghĩa
developer vocabulary; không cam kết nó là interpreter shipping cuối cùng.

Host-thread tick queue, bounded payload/event depth, asynchronous UI intent và
shutdown drain là contract dự kiến. Script exceptions phải được ghi kèm owner,
entry point và capability; không unwind sang native game thread. Deadline/budget
enforcement và memory quota chưa triển khai.

Không tuyên bố sandbox: native mod được tin cậy chạy cùng process privileges.
Desktop Python cũng không sandbox; script Python có thể import modules ngoài
SDK. Việc SDK không expose pointer không tự ngăn script độc hại. Restriction
runtime cần kiểm tra riêng trước support untrusted scripting.

## Ví dụ desktop

```python
from bedrockforge import Host
with Host("build-native/libbedrockforge.dll", "test-data/scripts") as host:
    mod = host.mod("example")
    mod.register_item("example:gem", "Gem", "materials")
    chest = mod.container("chest_001", 108)
    mod.deposit(chest, 0, "example:gem", 8)
    print(mod.read(chest, 0))
```

Sau mutation lấy lại `container(key, capacity)` để có revision hiện tại. Gameplay
adapter chưa tồn tại nên deposit chỉ tạo logical values, không tạo item Minecraft.
