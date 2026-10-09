#include "runtime.hpp"
#include <iostream>
#include <random>
#ifdef _WIN32
#include <windows.h>
struct Library {
  HMODULE handle;
  explicit Library(const char* path) : handle(LoadLibraryA(path)) {
    if (!handle) throw std::runtime_error("LoadLibrary failed");
  }
  ~Library() { if (handle) FreeLibrary(handle); }
  void* symbol(const char* name) { return reinterpret_cast<void*>(GetProcAddress(handle, name)); }
};
#else
#include <dlfcn.h>
struct Library {
  void* handle;
  explicit Library(const char* path) : handle(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
    if (!handle) throw std::runtime_error(dlerror());
  }
  ~Library() { if (handle) dlclose(handle); }
  void* symbol(const char* name) { return dlsym(handle, name); }
};
#endif

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (0)
static bf_result fail_load(const bf_api*) { return BF_INVALID; }
static void unload_fixture() {}
static const bf_mod failed{sizeof(bf_mod), BF_API_VERSION, "failed_fixture", "0.1.0", "", fail_load, unload_fixture};

int main(int argc, char** argv) {
  try {
    CHECK(argc == 2);
    Library library(argv[1]);
    auto entry = reinterpret_cast<bf_mod_entry_fn>(library.symbol("bf_mod_entry"));
    CHECK(entry != nullptr);
    const bf_mod* plugin = entry();
    CHECK(plugin && std::string(plugin->id) == "framework_fixture");

    auto root = std::filesystem::temp_directory_path() /
        ("bf-loader-test-" + std::to_string(std::random_device{}()));
    {
      bf::Runtime runtime(root);
      runtime.start({plugin});
      auto items = runtime.registry.search("fixture");
      CHECK(items.size() == 1 && std::string(items[0].id) == "fixture:material");
      auto recipes = runtime.registry.lookup("fixture:material", false);
      CHECK(recipes.size() == 1 && std::string(recipes[0].id) == "fixture:recipe");
      CHECK(runtime.sessions.contains("framework_fixture"));
      runtime.stop();
      CHECK(!runtime.sessions.at("framework_fixture")->enabled);
    }
    {
      bf::Runtime runtime(root / "rollback");
      try { runtime.start({plugin, &failed}); CHECK(false); }
      catch (const bf::Error&) { CHECK(runtime.registry.items.empty()); }
    }
    {
      bf::Runtime runtime(root / "dependency");
      bf_mod missing{sizeof(bf_mod), BF_API_VERSION, "missing_fixture", "0.1.0", "absent", plugin->load, plugin->unload};
      try { runtime.start({&missing}); CHECK(false); }
      catch (const bf::Error& error) { CHECK(error.code == BF_CONFLICT); }
    }
    std::filesystem::remove_all(root);
    std::cout << "Native plugin discovery boundary, registration, lifecycle and rollback passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
