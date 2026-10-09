# So sánh các framework gameplay

| Hệ thống | Biên mở rộng | Ngôn ngữ developer | Authority/UI | Khả năng tái dùng |
|---|---|---|---|---|
| NetEase Mod SDK | Engine components/systems do China Edition cung cấp | Python; tutorial được kiểm tra ghi Python 2 | Client UI/events, server item/world APIs, Notify messaging | Patterns/API model; proprietary engine runtime không tái dùng |
| Inner Core | Java/JS APIs và JNI native glue, version-specific engine pack | JS/TS, Java, C++ qua toolchain | Callback, containers, saver, machine UI examples | Patterns; license/provenance nguồn tham khảo chưa rõ |
| Horizon | Android launcher/pack/native environment | Java host và native modules | Pack isolation, native library environment | Host model; không suy ra modern Bedrock gameplay compatibility |
| LeviLaunchroid/Preloader | Launcher + native SO lifecycle/hooks/input | Java host, C++ mods | Mod Menu/HUD, mod paths, isolated profiles | Bootstrap candidate; thiếu verified item/inventory/recipe boundary |
| Forge | Java game registry/lifecycle/event integration | Java | Game-side registries và events theo phiên bản Java Edition | Registry timing, namespace, dependencies; không port JVM game APIs sang Bedrock |
| Fabric Loader/Fabric API | Java loader, entrypoints; Fabric API events/registries | Java/JVM | API events và game registries theo version | Separation loader/API, event/provider model; không engine reuse |
| BedrockForge 0.1 | Versioned C ABI → engine-independent core; GAL native inventory chưa có adapter | C++ mods và Python 3 desktop facade | Logical journals; Android browser; Ruby engine item qua authored packs | Host/Ruby có device evidence 1.26.30.5; custom tabs/chests chưa nghiệm thu |

Forge và Fabric không được clone/build ở đây. Chỉ kiểm tra tài liệu chính thức:
[Forge events](https://docs.minecraftforge.net/en/latest/concepts/events/),
[Forge registries](https://docs.minecraftforge.net/en/latest/concepts/registries/),
[Fabric events](https://docs.fabricmc.net/develop/events),
[Fabric custom items](https://docs.fabricmc.net/develop/items/first-item).
Các link docs này thay đổi theo phiên bản; không dùng làm dependency build.
Nguồn immutable các dự án Bedrock nằm trong [SOURCES.md](SOURCES.md).

Thiết kế áp dụng vào loader của chúng ta, từ ba hệ thống, nằm trong
[ECOSYSTEM_LESSONS.md](../architecture/ECOSYSTEM_LESSONS.md).

Bài học: native loading chỉ là một tầng. Extensibility cần vocabulary ổn định cho
items, recipes, inventories, events, UI, persistence và messaging, cùng adapter
engine có compatibility evidence. BedrockForge triển khai vocabulary/core trước,
sau đó kiểm chứng từng capability trên game thay vì suy đoán API.

Đối chiếu sâu Inner Core/Horizon, gồm phân biệt Creative group với inventory tab,
JNI boundary và thay thế cách lọc tên của tab Ruby:
[INNER_CORE_HORIZON_ANALYSIS.md](INNER_CORE_HORIZON_ANALYSIS.md).
