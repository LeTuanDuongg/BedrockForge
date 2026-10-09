#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif
namespace bf {
static void require(bool ok, bf_result code, const char* reason) { if(!ok) throw Error(code,reason); }
std::string text(const char* p,size_t n) { require(p, BF_INVALID,"null string"); const auto end=static_cast<const char*>(std::memchr(p,0,n)); require(end,BF_INVALID,"unterminated string"); return {p,end}; }
void copy(char* p,size_t n,const std::string& s) { require(s.size()<n,BF_INVALID,"string too long"); std::memset(p,0,n); std::memcpy(p,s.data(),s.size()); }
void identifier(const std::string& id) {
  require(!id.empty()&&id.size()<128,BF_INVALID,"invalid identifier length");
  require(std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c==':'||c=='-'||c=='.';}),BF_INVALID,"invalid identifier");
  require(id!="."&&id!="..",BF_INVALID,"invalid identifier");
}
void Registry::add(const bf_item& item) {
  auto id=text(item.id,sizeof item.id); identifier(id); text(item.name,sizeof item.name); text(item.category,sizeof item.category); text(item.icon,sizeof item.icon);
  require(item.max_stack>0&&item.max_stack<=65535,BF_INVALID,"stack limit");
  require(!items.contains(id),BF_CONFLICT,"duplicate item"); items.emplace(id,item);item_index.clear();
}
void Registry::validate(const bf_stack& s) const {
  auto id=text(s.item,sizeof s.item); text(s.metadata,sizeof s.metadata);
  if(s.count==0) { require(id.empty()&&text(s.metadata,sizeof s.metadata).empty(),BF_INVALID,"noncanonical empty stack"); return; }
  auto it=items.find(id); require(it!=items.end(),BF_NOT_FOUND,"unknown item");
  require(s.count<=it->second.max_stack,BF_INVALID,"stack overflow");
}
void Registry::add(const bf_recipe& r) {
  auto id=text(r.id,sizeof r.id); identifier(id); identifier(text(r.provider,sizeof r.provider));
  require(!recipes.contains(id),BF_CONFLICT,"duplicate recipe");
  require(r.ingredient_count>0&&r.ingredient_count<=9&&r.output.count>0,BF_INVALID,"recipe shape"); validate(r.output);
  for(uint32_t i=0;i<r.ingredient_count;i++){require(r.ingredients[i].count>0,BF_INVALID,"empty ingredient");validate(r.ingredients[i]);}
  recipes.emplace(id,r);recipe_index.clear();
}
bf_item Registry::item_at(uint32_t index) const {
 require(index<items.size(),BF_NOT_FOUND,"catalog end");
 if(item_index.size()!=items.size()){item_index.clear();for(const auto& [id,item]:items)item_index.push_back(id);}
 return items.at(item_index[index]);
}
bf_recipe Registry::recipe_at(uint32_t index) const {
 require(index<recipes.size(),BF_NOT_FOUND,"recipe end");
 if(recipe_index.size()!=recipes.size()){recipe_index.clear();for(const auto& [id,recipe]:recipes)recipe_index.push_back(id);}
 return recipes.at(recipe_index[index]);
}
static std::string lower(std::string s) { for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::vector<bf_item> Registry::search(std::string q,const std::string& category) const {
  q=lower(q); std::vector<bf_item> found;
  for(const auto& [id,item]:items) if((category.empty()||category==item.category)&&(lower(id).find(q)!=std::string::npos||lower(item.name).find(q)!=std::string::npos)) found.push_back(item);
  return found;
}
std::vector<bf_recipe> Registry::lookup(const std::string& item,bool usage) const {
  std::vector<bf_recipe> found;
  for(const auto& [id,r]:recipes){bool match=!usage&&item==r.output.item; if(usage)for(uint32_t i=0;i<r.ingredient_count;i++)match|=item==r.ingredients[i].item; if(match)found.push_back(r);} return found;
}
static uint32_t hash(const std::string& s) { uint32_t h=2166136261u;for(unsigned char c:s){h^=c;h*=16777619u;}return h; }
static std::string encode(const std::map<std::string,Container>& cs,uint64_t generation) {
  std::ostringstream o; o<<"BF1 "<<generation<<' '<<cs.size()<<'\n';
  for(const auto& [key,c]:cs){o<<std::quoted(key)<<' '<<std::quoted(c.id)<<' '<<c.revision<<' '<<c.slots.size()<<'\n';
    for(const auto& s:c.slots)o<<std::quoted(std::string(s.item))<<' '<<std::quoted(std::string(s.metadata))<<' '<<s.count<<'\n';}return o.str();
}
static std::map<std::string,Container> decode(const std::string& data,uint64_t& generation,const Registry& r) {
  std::istringstream in(data);std::string magic;size_t count;
  require(bool(in>>magic>>generation>>count)&&magic=="BF1"&&count<=1024,BF_INVALID,"bad save header");
  std::map<std::string,Container> cs;
  for(size_t n=0;n<count;n++){std::string key;Container c;size_t size;
    require(bool(in>>std::quoted(key)>>std::quoted(c.id)>>c.revision>>size)&&size>0&&size<=108,BF_INVALID,"bad container");identifier(key);identifier(c.id);c.slots.resize(size);
    for(auto& s:c.slots){std::string id,meta;require(bool(in>>std::quoted(id)>>std::quoted(meta)>>s.count),BF_INVALID,"bad slot");copy(s.item,sizeof s.item,id);copy(s.metadata,sizeof s.metadata,meta);r.validate(s);}
    require(cs.emplace(key,std::move(c)).second,BF_INVALID,"duplicate save key");}
  in>>std::ws;require(in.eof(),BF_INVALID,"trailing save data");return cs;
}
Store::Store(std::filesystem::path p,const Registry& r,bool defer):path_(std::move(p)),registry_(r),loaded_(!defer) { if(!defer&&std::filesystem::exists(path_))reload(); }
void Store::reload() {
  require(std::filesystem::file_size(path_)<=16*1024*1024,BF_INVALID,"journal size limit");
  std::ifstream in(path_,std::ios::binary); require(bool(in),BF_IO,"open journal");
  std::map<std::string,Container> recovered;uint64_t gen=0;uintmax_t bytes=0;
  while(in.peek()!=EOF){uint32_t len=0,sum=0;in.read(reinterpret_cast<char*>(&len),4);in.read(reinterpret_cast<char*>(&sum),4);
    if(!in)break;require(len>0&&len<=1024*1024,BF_INVALID,"invalid journal record size");
    std::string payload(len,'\0');in.read(payload.data(),len);if(!in)break;
    require(hash(payload)==sum,BF_INVALID,"journal checksum mismatch: quarantine required");uint64_t next;
    auto candidate=decode(payload,next,registry_); require(next==gen+1,BF_INVALID,"journal sequence");gen=next;recovered=std::move(candidate);bytes+=8+len;
  }
  require(bytes>0||std::filesystem::file_size(path_)==0,BF_INVALID,"no committed snapshot");
  containers=std::move(recovered);generation_=gen;valid_bytes_=bytes;
}
void Store::commit(std::map<std::string,Container> candidate) {
  require(!poisoned_,BF_IO,"store requires restart after uncertain write");
  require(candidate.size()<=1024,BF_INVALID,"container count limit");
  auto data=encode(candidate,generation_+1);require(data.size()<=1024*1024,BF_INVALID,"snapshot record limit");require(valid_bytes_+8+data.size()<=16*1024*1024,BF_IO,"journal full: export and compact offline");
  poisoned_=true;
  if(!path_.parent_path().empty())std::filesystem::create_directories(path_.parent_path());
  if(std::filesystem::exists(path_))std::filesystem::resize_file(path_,valid_bytes_); // remove incomplete tail before appending
  FILE* file=std::fopen(path_.string().c_str(),"ab");require(file,BF_IO,"open save");uint32_t len=static_cast<uint32_t>(data.size()),sum=hash(data);
  bool ok=std::fwrite(&len,4,1,file)==1&&std::fwrite(&sum,4,1,file)==1&&std::fwrite(data.data(),1,len,file)==len&&std::fflush(file)==0;
#ifdef _WIN32
  if(ok)ok=_commit(_fileno(file))==0;
#else
  if(ok)ok=fsync(fileno(file))==0;
#endif
  if(std::fclose(file)!=0)ok=false;
  require(ok,BF_IO,"durable save failed; restart and recover before further mutations");
#ifndef _WIN32
  // Persist newly created directory entry as well as file contents.
  auto parent=path_.parent_path().empty()?std::filesystem::path("."):path_.parent_path();
  int fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY);require(fd>=0,BF_IO,"open save directory");int synced=fsync(fd);close(fd);require(synced==0,BF_IO,"sync save directory");
#endif
  valid_bytes_+=8+len;generation_++;containers=std::move(candidate);poisoned_=false;
}
void Store::create(const std::string& key,uint32_t slots) {
  identifier(key);require(slots>0&&slots<=108,BF_INVALID,"capacity");
  if(!loaded_){if(std::filesystem::exists(path_))reload();loaded_=true;}
  if(containers.contains(key)){require(containers.at(key).slots.size()==slots,BF_CONFLICT,"existing capacity differs");return;}
  auto cs=containers;Container c;c.id=key;c.slots.resize(slots);cs.emplace(key,std::move(c));commit(std::move(cs));
}
static Container& get(std::map<std::string,Container>& cs,const std::string& key,uint32_t slot,uint64_t rev) {
  auto it=cs.find(key);require(it!=cs.end(),BF_NOT_FOUND,"container missing");auto& c=it->second;
  require(slot<c.slots.size(),BF_INVALID,"slot out of range");require(rev==c.revision,BF_CONFLICT,"stale inventory view");return c;
}
static bool same(const bf_stack& a,const bf_stack& b){return std::strcmp(a.item,b.item)==0&&std::strcmp(a.metadata,b.metadata)==0;}
static void insert(bf_stack& target,const bf_stack& source,const Registry& r){
  require(source.count>0,BF_INVALID,"zero deposit");r.validate(source);
  if(target.count==0)target=source;else {require(same(target,source),BF_CONFLICT,"different stack metadata");const auto max=r.items.at(source.item).max_stack;require(source.count<=max-target.count,BF_CONFLICT,"slot full");target.count+=source.count;}
}
void Store::deposit(const std::string& k,uint32_t slot,const bf_stack& s,uint64_t rev){auto cs=containers;auto& c=get(cs,k,slot,rev);insert(c.slots[slot],s,registry_);c.revision++;commit(std::move(cs));}
bf_stack Store::withdraw(const std::string& k,uint32_t slot,uint32_t count,uint64_t rev){auto cs=containers;auto& c=get(cs,k,slot,rev);auto& s=c.slots[slot];require(count>0&&count<=s.count,BF_INVALID,"withdraw count");auto result=s;result.count=count;s.count-=count;if(!s.count)s={};c.revision++;commit(std::move(cs));return result;}
void Store::transfer(const std::string& a,uint32_t ai,uint64_t ar,const std::string& b,uint32_t bi,uint64_t br,uint32_t count){
  require(a!=b||ai!=bi,BF_INVALID,"same slot");auto cs=containers;auto& ac=get(cs,a,ai,ar);auto& bc=get(cs,b,bi,br);auto& s=ac.slots[ai];
  require(count>0&&count<=s.count,BF_INVALID,"transfer count");auto moved=s;moved.count=count;insert(bc.slots[bi],moved,registry_);s.count-=count;if(!s.count)s={};ac.revision++;if(a!=b)bc.revision++;commit(std::move(cs));
}
Page paginate(uint32_t slots,uint32_t page,uint32_t width){require(slots>0&&slots<=108&&width>=48,BF_INVALID,"layout dimensions");uint32_t columns=std::min(9u,std::max(1u,width/48)),capacity=columns*3,pages=(slots+capacity-1)/capacity;require(page<pages,BF_INVALID,"page out of range");return {page,pages,page*capacity,std::min(capacity,slots-page*capacity),columns};}
uint64_t Events::subscribe(std::string owner,std::string topic,std::function<void(const std::string&)> fn){identifier(topic);require(bool(fn)&&listeners_.size()<4096,BF_INVALID,"listener limit");auto id=next_++;listeners_.emplace(id,Listener{std::move(owner),std::move(topic),std::move(fn)});return id;}
void Events::remove(uint64_t id,const std::string& owner){auto it=listeners_.find(id);require(it!=listeners_.end()&&it->second.owner==owner,BF_NOT_FOUND,"listener owner");listeners_.erase(it);}
void Events::clear(const std::string& owner){std::erase_if(listeners_,[&](const auto& p){return p.second.owner==owner;});}
void Events::publish(const std::string& topic,const std::string& payload){require(payload.size()<=65536&&depth_<16,BF_INVALID,"event limit");depth_++;auto snapshot=listeners_;try {for(const auto& [id,l]:snapshot)if(listeners_.contains(id)&&l.topic==topic)l.callback(payload);}catch(...){depth_--;throw;}depth_--;}
}
