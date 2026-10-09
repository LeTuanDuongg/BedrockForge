#pragma once
#include "core.hpp"
#include <memory>
#include <set>
namespace bf {
class Runtime;
struct Session {
  Runtime* runtime;
  std::string owner;
  bool enabled=true;
  bf_api api{};
  std::unique_ptr<Store> store;
  std::map<bf_handle,std::string> handles;
  bf_handle next_handle=1;
};
class BF_CORE Runtime {
  std::filesystem::path root_;
  bool safe_;
  std::vector<const bf_mod*> order_;
public:
  Registry registry;
  Events events;
  std::vector<std::string> diagnostics;
  std::map<std::string,std::unique_ptr<Session>> sessions;
  explicit Runtime(std::filesystem::path root,bool safe=false):root_(std::move(root)),safe_(safe){}
  ~Runtime();
  Session& session(const std::string& owner);
  void start(const std::vector<const bf_mod*>& mods);
  void stop();
  void tick(uint64_t tick);
};
}
