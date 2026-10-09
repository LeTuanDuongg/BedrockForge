#include "runtime.hpp"
#include <memory>
namespace {
uint64_t next_id=1;
std::map<uint64_t,std::unique_ptr<bf::Runtime>> hosts;
struct Binding {uint64_t host;bf::Session* session;};
std::map<uint64_t,Binding> bindings;
bf::Session* session(uint64_t id){auto it=bindings.find(id);return it==bindings.end()?nullptr:it->second.session;}
}
extern "C" {
BF_EXPORT uint64_t bf_script_host_create(const char* root){try{auto id=next_id++;hosts.emplace(id,std::make_unique<bf::Runtime>(bf::text(root,1024)));return id;}catch(...){return 0;}}
BF_EXPORT void bf_script_host_destroy(uint64_t id){std::erase_if(bindings,[&](const auto& p){return p.second.host==id;});hosts.erase(id);}
BF_EXPORT uint64_t bf_script_bind(uint64_t host,const char* owner){try{auto h=hosts.find(host);if(h==hosts.end())return 0;auto& s=h->second->session(bf::text(owner,128));auto id=next_id++;bindings.emplace(id,Binding{host,&s});return id;}catch(...){return 0;}}
BF_EXPORT bf_result bf_script_start_native(uint64_t host,uint32_t count,const bf_mod* const* mods){try{auto h=hosts.find(host);if(h==hosts.end()||!mods||count>128)return BF_INVALID;h->second->start(std::vector<const bf_mod*>(mods,mods+count));return BF_OK;}catch(const bf::Error& e){return e.code;}catch(...){return BF_INTERNAL;}}
BF_EXPORT bf_result bf_script_capability(uint64_t id,const char* name){auto s=session(id);return s?s->api.capability(s,name):BF_DISABLED;}
BF_EXPORT bf_result bf_script_item_register(uint64_t id,const bf_item* item){auto s=session(id);return s?s->api.register_item(s,item):BF_DISABLED;}
BF_EXPORT bf_result bf_script_item_at(uint64_t id,uint32_t index,bf_item* out){auto s=session(id);return s?s->api.item_at(s,index,out):BF_DISABLED;}
BF_EXPORT bf_result bf_script_container(uint64_t id,const char* key,uint32_t slots,bf_container* out){auto s=session(id);return s?s->api.create_container(s,key,slots,out):BF_DISABLED;}
BF_EXPORT bf_result bf_script_read(uint64_t id,uint64_t h,uint32_t slot,bf_stack* out){auto s=session(id);return s?s->api.read_slot(s,h,slot,out):BF_DISABLED;}
BF_EXPORT bf_result bf_script_deposit(uint64_t id,uint64_t h,uint32_t slot,const bf_stack* stack,uint64_t revision){auto s=session(id);return s?s->api.deposit(s,h,slot,stack,revision):BF_DISABLED;}
BF_EXPORT bf_result bf_script_withdraw(uint64_t id,uint64_t h,uint32_t slot,uint32_t count,uint64_t revision,bf_stack* out){auto s=session(id);return s?s->api.withdraw(s,h,slot,count,revision,out):BF_DISABLED;}
BF_EXPORT bf_result bf_script_recipe_register(uint64_t id,const bf_recipe* recipe){auto s=session(id);return s?s->api.register_recipe(s,recipe):BF_DISABLED;}
BF_EXPORT bf_result bf_script_recipe_at(uint64_t id,uint32_t index,bf_recipe* out){auto s=session(id);return s?s->api.recipe_at(s,index,out):BF_DISABLED;}
}
