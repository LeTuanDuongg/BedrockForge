#pragma once
#include "bedrockforge.h"
#include <stdexcept>
namespace bedrockforge {
inline void check(bf_result code){if(code!=BF_OK)throw std::runtime_error("BedrockForge service rejected operation");}
class Subscription {
  const bf_api* api_=nullptr;
  bf_handle handle_=0;
public:
  Subscription(const bf_api& api,const char* topic,bf_event_fn callback,void* user):api_(&api){check(api.subscribe(api.context,topic,callback,user,&handle_));}
  Subscription(const Subscription&)=delete;
  Subscription& operator=(const Subscription&)=delete;
  ~Subscription(){if(api_)api_->unsubscribe(api_->context,handle_);}
};
/* Destroy wrappers before mod unload; the table belongs to the runtime session. */
}
