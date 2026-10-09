#pragma once
#include "bedrockforge.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
namespace bf {
#ifdef _WIN32
#ifdef BF_BUILD
#define BF_CORE __declspec(dllexport)
#else
#define BF_CORE __declspec(dllimport)
#endif
#else
#define BF_CORE
#endif
struct Error : std::runtime_error { bf_result code; Error(bf_result c, const std::string& s):runtime_error(s),code(c){} };
BF_CORE std::string text(const char* p, size_t n);
BF_CORE void copy(char* target, size_t n, const std::string& value);
BF_CORE void identifier(const std::string& id);
struct BF_CORE Registry {
  std::map<std::string,bf_item> items;
  std::map<std::string,bf_recipe> recipes;
  mutable std::vector<std::string> item_index,recipe_index;
  bf_item item_at(uint32_t) const;
  bf_recipe recipe_at(uint32_t) const;
  void add(const bf_item&);
  void add(const bf_recipe&);
  void validate(const bf_stack&) const;
  std::vector<bf_item> search(std::string query, const std::string& category="") const;
  std::vector<bf_recipe> lookup(const std::string& item, bool usage) const;
};
struct Container { std::string id; uint64_t revision=0; std::vector<bf_stack> slots; };
/* One store per owner/world, one journal for atomic multi-container commits. */
class BF_CORE Store {
  std::filesystem::path path_;
  const Registry& registry_;
  uint64_t generation_=0;
  uintmax_t valid_bytes_=0;
  bool loaded_=true, poisoned_=false;
  void commit(std::map<std::string,Container> candidate);
public:
  std::map<std::string,Container> containers;
  Store(std::filesystem::path path, const Registry& registry, bool defer_load=false);
  void reload();
  void create(const std::string& key, uint32_t slots);
  void deposit(const std::string&, uint32_t, const bf_stack&, uint64_t);
  bf_stack withdraw(const std::string&, uint32_t, uint32_t count, uint64_t);
  void transfer(const std::string&, uint32_t, uint64_t, const std::string&, uint32_t, uint64_t, uint32_t);
};
struct Page { uint32_t page, pages, first, count, columns; };
BF_CORE Page paginate(uint32_t slots, uint32_t page, uint32_t width_dp);
class BF_CORE Events {
  struct Listener { std::string owner,topic; std::function<void(const std::string&)> callback; };
  std::map<uint64_t,Listener> listeners_;
  uint64_t next_=1;
  uint32_t depth_=0;
public:
  uint64_t subscribe(std::string owner,std::string topic,std::function<void(const std::string&)> callback);
  void remove(uint64_t id, const std::string& owner);
  void clear(const std::string& owner);
  void publish(const std::string& topic,const std::string& payload);
};
}
