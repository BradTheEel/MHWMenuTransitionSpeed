#include "mts_internal.h"
#include "mts_build_15539686.h"

/* Optional Hunter's Notes opening acceleration. */
typedef uint8_t (*HunterNoteTick)(void *);
static HunterNoteTick hunter_note_original_tick;
static volatile LONG hunter_note_active;

enum {
    hunter_note_action_tick_rva = 0x1786a80u,
    hunter_note_action_cleanup_rva = 0x1784ec0u
};

int hunter_note_is_active(void)
{
    return InterlockedCompareExchange(&hunter_note_active, 0, 0);
}

static uint8_t hunter_note_tick(void *action)
{
    enum {
        action_actor_offset = 0x28,
        action_mode_offset = 0x30,
        action_phase_offset = 0x38,
        action_gui_dispatched_offset = 0x3c,
        actor_flags_offset = 0x0c,
        actor_primary_rate_offset = 0xe188,
        actor_secondary_rate_offset = 0xe190
    };
    uint8_t result = hunter_note_original_tick(action);
    __try {
        unsigned char *a = (unsigned char *)action;
        uintptr_t b = mts_game_base;
        if (!hunter_note_is_active() ||
            mts_bypass_held() || !mts_readable(a, 0x3d) ||
            *(uintptr_t *)a != b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA ||
            *(uint32_t *)(a + action_mode_offset) != 1u ||
            *(uint32_t *)(a + action_phase_offset) != 0u || a[action_gui_dispatched_offset] != 0u ||
            !mts_readable((void *)(b + MTS_LOCAL_PLAYER_MANAGER_RVA), 8)) return result;
        void *manager = *(void **)(b + MTS_LOCAL_PLAYER_MANAGER_RVA);
        if (!mts_readable(manager, 8)) return result;
        unsigned char *actor = *(unsigned char **)(a + action_actor_offset);
        if (!mts_readable(actor, 16) || *(uintptr_t *)actor != b + MTS_LOCAL_PLAYER_VTABLE_RVA ||
            !(actor[actor_flags_offset] & 0xe) || ((MtsLocalPlayerGetter)(b + MTS_LOCAL_PLAYER_GETTER_RVA))(manager) != actor ||
            !mts_writable(actor + actor_primary_rate_offset, 4) ||
            !mts_writable(actor + actor_secondary_rate_offset, 4)) return result;
        /* The actor rate fields hold floats. Compare and write their exact bits. */
        if (*(uint32_t *)(actor + actor_primary_rate_offset) != 0x3f800000u ||
            *(uint32_t *)(actor + actor_secondary_rate_offset) != 0x3f800000u ||
            *(void **)(b + MTS_LOCAL_PLAYER_MANAGER_RVA) != manager ||
            *(unsigned char **)(a + action_actor_offset) != actor ||
            *(uintptr_t *)a != b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA ||
            *(uint32_t *)(a + action_mode_offset) != 1u ||
            *(uint32_t *)(a + action_phase_offset) != 0u || a[action_gui_dispatched_offset] != 0u ||
            *(uintptr_t *)actor != b + MTS_LOCAL_PLAYER_VTABLE_RVA || !(actor[actor_flags_offset] & 0xe) ||
            mts_bypass_held()) return result;
        *(uint32_t *)(actor + actor_primary_rate_offset) = 0x40c00000u;
        *(uint32_t *)(actor + actor_secondary_rate_offset) = 0x40c00000u;
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Native result remains authoritative. */ }
    return result;
}

static int hunter_note_supported(uintptr_t b)
{
    enum {
        action_tick_slot_offset = 0x38,
        action_cleanup_slot_offset = 0x40,
        page_tick_slot_offset = 0x48
    };
    if (!mts_readable((void *)(b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA + action_tick_slot_offset), 16) ||
        !mts_readable((void *)(b + MTS_HUNTER_NOTE_UI_VTABLE_RVA + page_tick_slot_offset), 8) ||
        !mts_readable((void *)(b + MTS_HUNTER_NOTE_RIGHT_VTABLE_RVA + page_tick_slot_offset), 8)) return 0;
    if (*(uintptr_t *)(b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA + action_tick_slot_offset) != b + hunter_note_action_tick_rva ||
        *(uintptr_t *)(b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA + action_cleanup_slot_offset) != b + hunter_note_action_cleanup_rva ||
        *(uintptr_t *)(b + MTS_HUNTER_NOTE_UI_VTABLE_RVA + page_tick_slot_offset) != b + MTS_COMMON_TICK_RVA ||
        *(uintptr_t *)(b + MTS_HUNTER_NOTE_RIGHT_VTABLE_RVA + page_tick_slot_offset) != b + MTS_COMMON_TICK_RVA) return 0;
    return mts_region_hash(b, hunter_note_action_tick_rva, 0x508u, UINT64_C(0x46f59ff0595f8a83)) &&
        mts_region_hash(b, hunter_note_action_cleanup_rva, 0x31u, UINT64_C(0xed4d79a3c0538460)) &&
        mts_region_hash(b, MTS_LOCAL_PLAYER_GETTER_RVA, MTS_LOCAL_PLAYER_GETTER_SIZE, MTS_LOCAL_PLAYER_GETTER_HASH) &&
        mts_region_hash(b, 0x1f65ec1u, 0x6bu, UINT64_C(0x63b805ab1ecb0282));
}

void hunter_note_install(uintptr_t b)
{
    void *target = (void *)(b + hunter_note_action_tick_rva);
    __try {
        if (!hunter_note_supported(b)) return;
        if (MH_CreateHook(target, (void *)hunter_note_tick, (void **)&hunter_note_original_tick) != MH_OK) return;
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            return;
        }
        InterlockedExchange(&hunter_note_active, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Optional feature stays disabled. */ }
}
