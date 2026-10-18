#pragma once

#include <cstdint>

extern "C" const char* hybrid_experimental_hook_self_test();
extern "C" const char* hybrid_experimental_engine_candidate_hook_start();
extern "C" const char* hybrid_experimental_engine_candidate_hook_stop();
extern "C" const char* hybrid_experimental_cobblestone_constructor_hook_start();
extern "C" const char* hybrid_experimental_cobblestone_constructor_hook_stop();
extern "C" int hybrid_experimental_cobblestone_constructor_match(std::uintptr_t object);
extern "C" int hybrid_experimental_block_construction_arm();
extern "C" int hybrid_experimental_button_control(unsigned operation);
