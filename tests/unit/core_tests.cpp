#include "core.hpp"
#include "runtime.hpp"
#include <iostream>
#include <fstream>
#include <random>
#define CHECK(x) do { if(!(x))throw std::runtime_error("check failed: " #x); ++checks; } while(0)
int checks=0;
template<class F> void fails(F f,bf_result code){try{f();throw std::runtime_error("expected error");}catch(const bf::Error& e){CHECK(e.code==code);}}
bf_item item(const char* id,uint32_t max=64){bf_item i{};bf::copy(i.id,sizeof i.id,id);bf::copy(i.name,sizeof i.name,id);bf::copy(i.category,sizeof i.category,"building");i.max_stack=max;return i;}
bf_stack stack(const char* id,uint32_t count,const char* meta=""){bf_stack s{};bf::copy(s.item,sizeof s.item,id);bf::copy(s.metadata,sizeof s.metadata,meta);s.count=count;return s;}
int main(){try{
  auto root=std::filesystem::temp_directory_path()/("bf-test-"+std::to_string(std::random_device{}()));std::filesystem::create_directories(root);
  bf::Registry r;r.add(item("test:stone"));r.add(item("test:tool",1));
  fails([&]{r.add(item("test:stone"));},BF_CONFLICT);fails([&]{r.validate(stack("test:stone",65));},BF_INVALID);
  CHECK(r.search("STONE").size()==1);CHECK(r.search("","building").size()==2);CHECK(r.search("missing").empty());
  bf_recipe recipe{};bf::copy(recipe.id,sizeof recipe.id,"test:recipe");bf::copy(recipe.provider,sizeof recipe.provider,"test");recipe.output=stack("test:tool",1);recipe.ingredient_count=1;recipe.ingredients[0]=stack("test:stone",3);r.add(recipe);
  CHECK(r.lookup("test:tool",false).size()==1);CHECK(r.lookup("test:stone",true).size()==1);CHECK(r.lookup("missing",true).empty());
  for(uint32_t capacity:{54u,81u,108u}){
    auto path=root/(std::to_string(capacity)+".journal");bf::Store s(path,r);s.create("chest",capacity);s.create("other",capacity);
    for(uint32_t i=0;i<capacity;i++)s.deposit("chest",i,stack("test:stone",63),s.containers.at("chest").revision);
    CHECK(s.containers.at("chest").slots.size()==capacity);
    auto rev=s.containers.at("chest").revision;s.deposit("chest",0,stack("test:stone",1),rev);
    fails([&]{s.deposit("chest",0,stack("test:stone",1),rev);},BF_CONFLICT);
    rev=s.containers.at("chest").revision;fails([&]{s.deposit("chest",0,stack("test:stone",1),rev);},BF_CONFLICT);
    fails([&]{s.withdraw("chest",capacity,1,rev);},BF_INVALID);fails([&]{s.withdraw("chest",0,0,rev);},BF_INVALID);
    s.transfer("chest",0,rev,"other",0,0,20);CHECK(s.containers.at("chest").slots[0].count==44);CHECK(s.containers.at("other").slots[0].count==20);
    auto removed=s.withdraw("other",0,20,1);CHECK(removed.count==20);CHECK(s.containers.at("other").slots[0].item[0]==0);
    bf::Store reload(path,r);CHECK(reload.containers.at("chest").slots[0].count==44);
    {std::ofstream o(path,std::ios::binary|std::ios::app);o.write("bad",3);}bf::Store interrupted(path,r);CHECK(interrupted.containers.at("chest").slots[0].count==44);
    interrupted.deposit("other",1,stack("test:stone",1),2);bf::Store recovered(path,r);CHECK(recovered.containers.at("other").slots[1].count==1);
    uint32_t displayed=0;auto first=bf::paginate(capacity,0,360);for(uint32_t p=0;p<first.pages;p++)displayed+=bf::paginate(capacity,p,360).count;CHECK(displayed==capacity);fails([&]{bf::paginate(capacity,first.pages,360);},BF_INVALID);
  }
  bf::Store tx(root/"tx",r);tx.create("a",54);tx.create("b",54);tx.deposit("a",0,stack("test:stone",40,"enchanted"),0);tx.deposit("b",0,stack("test:stone",40),0);
  fails([&]{tx.transfer("a",0,1,"b",0,1,5);},BF_CONFLICT);CHECK(tx.containers.at("a").slots[0].count==40);
  // Deterministic randomized transfers conserve every item, including failed full-slot moves.
  tx.create("c",54);tx.deposit("c",0,stack("test:stone",64),0);std::mt19937 random(42);
  for(int i=0;i<250;i++){uint32_t a=random()%54,b=random()%54;if(a==b)continue;auto rev=tx.containers.at("c").revision;auto count=tx.containers.at("c").slots[a].count;if(count)try{tx.transfer("c",a,rev,"c",b,rev,1+random()%count);}catch(const bf::Error&){}uint32_t total=0;for(auto s:tx.containers.at("c").slots)total+=s.count;CHECK(total==64);}
  auto corrupt=root/"corrupt";{std::ofstream o(corrupt,std::ios::binary);uint32_t len=3,checksum=0;o.write(reinterpret_cast<char*>(&len),4);o.write(reinterpret_cast<char*>(&checksum),4);o.write("xxx",3);}fails([&]{bf::Store bad(corrupt,r);},BF_INVALID);
  bf::Events events;int called=0;events.subscribe("test","framework:tick",[&](const std::string&){called++;});events.publish("framework:tick","1");CHECK(called==1);events.clear("test");events.publish("framework:tick","2");CHECK(called==1);
  bf::Runtime runtime(root/"runtime");auto& session=runtime.session("test");CHECK(session.api.capability(&session,"game.inventory.v1")==BF_UNSUPPORTED);CHECK(session.api.ui_open(&session,"x","{}")==BF_UNSUPPORTED);runtime.stop();CHECK(session.api.capability(&session,"core.items.v1")==BF_DISABLED);
  bf::Runtime safe(root/"safe",true);fails([&]{safe.start({});},BF_DISABLED);
  std::filesystem::remove_all(root);std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
