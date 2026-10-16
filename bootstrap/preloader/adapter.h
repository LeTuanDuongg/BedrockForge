#pragma once
#include <jni.h>
#include <cstdint>

// Private interoperability layout; no Preloader implementation or SDK is linked.
// Contract: preloader-android 5d108da350ce3e7f13f51415f81e65344ecd5d14, LegacyMod.h.
struct PreloaderMetadata {
    uint32_t size;
    const char* mod_id;
    const char* display_name;
    const char* author;
    const char* version;
    const char* entry_path;
    const char* entry_file_name;
    const char* library_path;
    const char* icon_path;
    const char* manifest_path;
    const char* mod_root_path;
};
static_assert(sizeof(void*) != 8 || sizeof(PreloaderMetadata) == 88);

extern "C" {
bool PLMod_Load(JavaVM*, const PreloaderMetadata*);
bool PLMod_Enable();
bool PLMod_Disable();
bool PLMod_Unload();
}
