#include "runtime.hpp"
#include <cstring>
#include <algorithm>
namespace bf {
static Session& active(void* ctx){if(!ctx)throw Error(BF_INVALID,"null context");auto& s=*static_cast<Session*>(ctx);if(!s.enabled)throw Error(BF_DISABLED,"mod disabled");return s;}
template<class F> static bf_result guard(void* ctx,F fn) noexcept {
  try{auto& s=active(ctx);fn(s);return BF_OK;}catch(const Error& e){return e.code;}catch(const std::filesystem::filesystem_error&){return BF_IO;}catch(...){return BF_INTERNAL;}
}
static std::string str(const char* p){return text(p,1024);}
static std::string key(Session& s,bf_handle h){auto i=s.handles.find(h);if(i==s.handles.end())throw Error(BF_NOT_FOUND,"unknown handle");return i->second;}
static void info(Session& s,bf_handle h,bf_container* out){if(!out)throw Error(BF_INVALID,"null output");auto k=key(s,h);auto& c=s.store->containers.at(k);*out={};out->handle=h;out->slots=static_cast<uint32_t>(c.slots.size());out->revision=c.revision;copy(out->identity,sizeof out->identity,s.owner+":"+c.id);}
Session& Runtime::session(const std::string& owner){
  identifier(owner);if(sessions.contains(owner))return *sessions.at(owner);
  auto ptr=std::make_unique<Session>();auto& s=*ptr;s.runtime=this;s.owner=owner;
  s.store=std::make_unique<Store>(root_/(owner+".bfjournal"),registry,true);
  auto& a=s.api;a.size=sizeof a;a.version=BF_API_VERSION;a.context=&s;
  a.capability=[](void* c,const char* name)->bf_result {return guard(c,[&](Session&){auto n=str(name);if(n!="core.inventory.v1"&&n!="core.items.v1"&&n!="core.recipes.v1"&&n!="core.events.v1"&&n!="core.persistence.v1")throw Error(BF_UNSUPPORTED,"capability unavailable");});};
  a.log=[](void* c,const char* line){guard(c,[&](Session& s){s.runtime->diagnostics.push_back(s.owner+": "+str(line));});};
  a.register_item=[](void* c,const bf_item* item)->bf_result{return guard(c,[&](Session& s){if(!item)throw Error(BF_INVALID,"null item");auto id=text(item->id,sizeof item->id);if(!id.starts_with(s.owner+":"))throw Error(BF_INVALID,"item namespace must match owner");s.runtime->registry.add(*item);});};
  a.item_at=[](void* c,uint32_t index,bf_item* out)->bf_result{return guard(c,[&](Session& s){if(!out)throw Error(BF_INVALID,"null output");*out=s.runtime->registry.item_at(index);});};
  a.register_recipe=[](void* c,const bf_recipe* r)->bf_result{return guard(c,[&](Session& s){if(!r)throw Error(BF_INVALID,"null recipe");if(!text(r->id,sizeof r->id).starts_with(s.owner+":")||text(r->provider,sizeof r->provider)!=s.owner)throw Error(BF_INVALID,"recipe ownership");s.runtime->registry.add(*r);});};
  a.recipe_at=[](void* c,uint32_t index,bf_recipe* out)->bf_result{return guard(c,[&](Session& s){if(!out)throw Error(BF_INVALID,"null output");*out=s.runtime->registry.recipe_at(index);});};
  a.create_container=[](void* c,const char* k,uint32_t slots,bf_container* out)->bf_result{return guard(c,[&](Session& s){if(!out)throw Error(BF_INVALID,"null output");auto name=str(k);if(s.owner.size()+name.size()+1>=128)throw Error(BF_INVALID,"identity length");s.store->create(name,slots);bf_handle h=0;for(const auto& [existing,n]:s.handles)if(n==name)h=existing;if(!h){h=s.next_handle++;s.handles.emplace(h,name);}info(s,h,out);});};
  a.container_info=[](void* c,bf_handle h,bf_container* out)->bf_result{return guard(c,[&](Session& s){info(s,h,out);});};
  a.read_slot=[](void* c,bf_handle h,uint32_t slot,bf_stack* out)->bf_result{return guard(c,[&](Session& s){auto& slots=s.store->containers.at(key(s,h)).slots;if(!out||slot>=slots.size())throw Error(BF_INVALID,"slot index");*out=slots[slot];});};
  a.deposit=[](void* c,bf_handle h,uint32_t slot,const bf_stack* stack,uint64_t rev)->bf_result{return guard(c,[&](Session& s){if(!stack)throw Error(BF_INVALID,"null stack");s.store->deposit(key(s,h),slot,*stack,rev);});};
  a.withdraw=[](void* c,bf_handle h,uint32_t slot,uint32_t count,uint64_t rev,bf_stack* out)->bf_result{return guard(c,[&](Session& s){if(!out)throw Error(BF_INVALID,"null output");*out=s.store->withdraw(key(s,h),slot,count,rev);});};
  a.transfer=[](void* c,bf_handle ah,uint32_t ai,uint64_t ar,bf_handle bh,uint32_t bi,uint64_t br,uint32_t count)->bf_result{return guard(c,[&](Session& s){s.store->transfer(key(s,ah),ai,ar,key(s,bh),bi,br,count);});};
  a.subscribe=[](void* c,const char* topic,bf_event_fn cb,void* user,bf_handle* out)->bf_result{return guard(c,[&](Session& s){if(!cb||!out)throw Error(BF_INVALID,"null callback");auto name=str(topic);*out=s.runtime->events.subscribe(s.owner,name,[cb,user,name](const std::string& p){cb(user,name.c_str(),p.c_str());});});};
  a.unsubscribe=[](void* c,bf_handle h)->bf_result{return guard(c,[&](Session& s){s.runtime->events.remove(h,s.owner);});};
  a.publish=[](void* c,const char* topic,const char* payload)->bf_result{return guard(c,[&](Session& s){auto name=str(topic);if(!name.starts_with(s.owner+":"))throw Error(BF_INVALID,"event ownership");s.runtime->events.publish(name,str(payload));});};
  a.ui_open=[](void* c,const char*,const char*)->bf_result{return guard(c,[](Session&){throw Error(BF_UNSUPPORTED,"no verified game UI adapter");});};
  a.message=[](void* c,const char*,const char*)->bf_result{return guard(c,[](Session&){throw Error(BF_UNSUPPORTED,"no verified transport");});};
  auto& result=s;sessions.emplace(owner,std::move(ptr));return result;
}
void Runtime::start(const std::vector<const bf_mod*>& mods){
  if(safe_){if(!mods.empty())throw Error(BF_DISABLED,"safe mode: plugin loading disabled");return;}
  if(!order_.empty()||!sessions.empty())throw Error(BF_CONFLICT,"runtime already started");
  std::map<std::string,const bf_mod*> pending;
  for(auto m:mods){if(!m||m->size<sizeof(bf_mod)||m->api_version!=BF_API_VERSION||!m->load||!m->unload)throw Error(BF_INVALID,"mod ABI mismatch");auto id=str(m->id);identifier(id);str(m->version);if(!pending.emplace(id,m).second)throw Error(BF_CONFLICT,"duplicate mod");}
  // Resolve whole graph before invoking arbitrary mod code.
  std::vector<const bf_mod*> sorted;std::set<std::string> ready;
  while(!pending.empty()){bool progress=false;for(auto it=pending.begin();it!=pending.end();){auto dep=str(it->second->required_mod);if(dep.empty()||ready.contains(dep)){ready.insert(it->first);sorted.push_back(it->second);it=pending.erase(it);progress=true;}else ++it;}if(!progress)throw Error(BF_CONFLICT,"dependency missing or cycle");}
  auto before=registry;
  try{for(auto m:sorted){auto& s=session(m->id);order_.push_back(m);auto result=m->load(&s.api);if(result!=BF_OK)throw Error(result,"mod load failed");}}
  catch(...){stop();registry=std::move(before);throw;}
}
void Runtime::stop(){for(auto it=order_.rbegin();it!=order_.rend();++it){auto owner=str((*it)->id);try{(*it)->unload();}catch(...){diagnostics.push_back(owner+": unload exception");}if(sessions.contains(owner)){sessions.at(owner)->enabled=false;events.clear(owner);}}
  order_.clear();for(auto& [owner,s]:sessions){s->enabled=false;events.clear(owner);} // keep context cookies alive until Runtime destruction
}
Runtime::~Runtime(){stop();}
void Runtime::tick(uint64_t n){events.publish("framework:tick",std::to_string(n));}
}
