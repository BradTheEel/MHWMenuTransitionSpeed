#ifndef MTS_INTERNAL_H
#define MTS_INTERNAL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include "vendor/include/MinHook.h"

extern uintptr_t mts_game_base;

int mts_readable(const void *p, SIZE_T n);
int mts_writable(void *p, SIZE_T n);
int mts_region_hash(uintptr_t base, uint32_t rva, uint32_t size, uint64_t expected);
int mts_bypass_held(void);

void quest_install(uintptr_t base);
void mts_resource_install(uintptr_t base);
void hunter_note_install(uintptr_t base);
void hunter_camera_install(uintptr_t base);

int mts_resource_is_active(void);
int hunter_note_is_active(void);

#endif
