#pragma once
#include <string>
#include <set>
namespace bf {
/* Contract only: no international Bedrock adapter has been verified. */
struct GameProfile { std::string version, abi, library_sha256; };
class GameAdapter {
public:
  virtual ~GameAdapter()=default;
  virtual unsigned gal_version() const noexcept=0;
  virtual bool validate(const GameProfile&) const=0;
  virtual std::set<std::string> capabilities() const=0;
};
class UnavailableAdapter final:public GameAdapter {
public:
  unsigned gal_version() const noexcept override{return 1;}
  bool validate(const GameProfile&)const override{return false;}
  std::set<std::string> capabilities()const override{return {};}
};
}
