#include "runtime.hpp"
#include <pl/Mod.hpp>
/* Audited PL_REGISTER_MOD lifecycle boundary. Untested on Android. */
class BedrockForgeHost {
  std::unique_ptr<bf::Runtime> runtime;
public:
  static BedrockForgeHost& instance(){static BedrockForgeHost host;return host;}
  bool load(){
    auto* self=ll::mod::NativeMod::current();if(!self)return false;
    try{
      auto root=self->getModDir();auto data=self->getConfigDir()/"isolated-logical-data";
      bool safe=std::filesystem::exists(self->getConfigDir()/"SAFE_MODE");
      runtime=std::make_unique<bf::Runtime>(data,safe);
      if(safe){self->getLogger().info("BedrockForge safe mode: runtime started without plugins");return true;}
      runtime->start({});self->getLogger().info("BedrockForge framework runtime loaded; profile plugin discovery is not integrated");return true;
    }catch(const std::exception& e){self->getLogger().error("BedrockForge load failed: {}",e.what());unload();return false;}
  }
  bool enable(){return runtime?true:load();}
  bool disable(){return unload();}
  bool unload(){runtime.reset();return true;}
};
PL_REGISTER_MOD(BedrockForgeHost,BedrockForgeHost::instance())
