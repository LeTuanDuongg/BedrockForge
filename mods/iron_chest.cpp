#include "hybrid_mod.h"
namespace {
constexpr unsigned slots=54;
static_assert(slots==6*9);
int start(const HybridHost*){return -1;} // No registration until real gameplay bindings exist.
void stop(){}
const HybridMod mod{HYBRID_ABI,sizeof(HybridMod),"hybrid:iron_chest",HYBRID_CAP_CONTAINER|HYBRID_CAP_CHEST_UI|HYBRID_CAP_PERSISTENCE,start,stop};
}
// Lifecycle is gated before start; no gameplay API is advertised until verified.
extern "C" const HybridMod* hybrid_mod_entry(){return &mod;}
