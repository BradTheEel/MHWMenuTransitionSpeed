#include "mts_internal.h"
#include "mts_build_15539686.h"

/* Optional Hunter's Notes camera motion acceleration. */
typedef void (*HunterCameraTick)(void *);
static HunterCameraTick hunter_camera_original_tick;
static volatile LONG hunter_camera_active;

enum {
    camera_motion_tick_rva = 0x231c760u,
    camera_motion_vtable_rva = 0x34970f0u,
    camera_action_30_vtable_rva = 0x3244748u,
    camera_action_31_vtable_rva = 0x32447a8u,
    motion_rate_offset = 0x1e0
};

static int hunter_camera_book(unsigned char *actor, uintptr_t b)
{
    enum {
        actor_first_action_manager_offset = 0x61c8u,
        actor_second_action_manager_offset = 0x6878u,
        manager_active_bank_offset = 0xac,
        manager_active_index_offset = 0xb0,
        manager_bank_table_offset = 0x68,
        manager_bank_count_offset = 0x70,
        manager_bank_stride = 16,
        book_action_actor_offset = 0x28,
        book_action_mode_offset = 0x30
    };
    const unsigned offsets[] = {actor_first_action_manager_offset, actor_second_action_manager_offset};
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *m = actor + offsets[i];
        if (!mts_readable(m, 0xb4)) continue;
        uint32_t bank = *(uint32_t *)(m + manager_active_bank_offset);
        uint32_t index = *(uint32_t *)(m + manager_active_index_offset);
        if (bank > 3) continue;
        unsigned char **table = *(unsigned char ***)(m + manager_bank_table_offset + bank * manager_bank_stride);
        uint32_t count = *(uint32_t *)(m + manager_bank_count_offset + bank * manager_bank_stride);
        if (index >= count || count > 4096 || !mts_readable(table, (SIZE_T)count * 8)) continue;
        unsigned char *action = table[index];
        if (mts_readable(action, 0x34) && *(uintptr_t *)action == b + MTS_HUNTER_NOTE_ACTION_VTABLE_RVA &&
            *(void **)(action + book_action_actor_offset) == actor &&
            *(uint32_t *)(action + book_action_mode_offset) == 1u) return 1;
    }
    return 0;
}

static int hunter_camera_eligible(void *motion, uintptr_t ret)
{
    enum {
        actor_flags_offset = 0x0c,
        actor_camera_offset = 0x7690,
        actor_view_offset = 0x14f8,
        camera_motion_offset = 0xb60,
        view_actor_offset = 0xdb0,
        view_controller_offset = 0x1410,
        controller_active_bank_offset = 0x1f74,
        controller_active_index_offset = 0x1f78,
        controller_bank_zero_table_offset = 0x1f30,
        controller_bank_zero_count_offset = 0x1f38,
        camera_action_camera_offset = 0x28,
        camera_action_controller_offset = 0x48,
        camera_action_view_offset = 0x50,
        motion_clip_flags_offset = 0x14,
        motion_clip_index_offset = 0x1d8,
        motion_current_frame_offset = 0x1dc,
        motion_clip_resource_offset = 0x1f0,
        resource_clip_table_offset = 0xa8,
        clip_table_count_offset = 6,
        clip_table_entries_offset = 8,
        clip_duration_frames_offset = 0x20
    };
    uintptr_t b = mts_game_base;
    unsigned char *p = (unsigned char *)motion;
    if (!InterlockedCompareExchange(&hunter_camera_active, 0, 0) ||
        (ret != b + 0x1fbeb0du && ret != b + 0x1fb139eu) ||
        mts_bypass_held() || !mts_readable((void *)(b + MTS_LOCAL_PLAYER_MANAGER_RVA), 8)) return 0;
    void *manager = *(void **)(b + MTS_LOCAL_PLAYER_MANAGER_RVA);
    if (!mts_readable(manager, 8)) return 0;
    unsigned char *actor = ((MtsLocalPlayerGetter)(b + MTS_LOCAL_PLAYER_GETTER_RVA))(manager);
    if (!mts_readable(actor, 0x7698) || *(uintptr_t *)actor != b + MTS_LOCAL_PLAYER_VTABLE_RVA ||
        !(actor[actor_flags_offset] & 0xe)) return 0;
    unsigned char *camera = *(unsigned char **)(actor + actor_camera_offset);
    unsigned char *view = *(unsigned char **)(actor + actor_view_offset);
    if (!mts_readable(camera, 0xb68) || *(uintptr_t *)camera != b + MTS_CAMERA_VTABLE_RVA ||
        camera + camera_motion_offset != p || !mts_readable(p, 0x1f8) ||
        *(uintptr_t *)p != b + camera_motion_vtable_rva || !mts_writable(p + motion_rate_offset, 4) ||
        !mts_readable(view, 0x33a0) || *(uintptr_t *)view != b + 0x322bbb0u ||
        *(void **)(view + view_actor_offset) != actor) return 0;
    unsigned char *ctl = view + view_controller_offset;
    uint32_t bank = *(uint32_t *)(ctl + controller_active_bank_offset);
    uint32_t index = *(uint32_t *)(ctl + controller_active_index_offset);
    if (bank != 0 || (index != 30 && index != 31)) return 0;
    unsigned char **table = *(unsigned char ***)(ctl + controller_bank_zero_table_offset);
    uint32_t count = *(uint32_t *)(ctl + controller_bank_zero_count_offset);
    if (index >= count || count > 4096 || !mts_readable(table, (SIZE_T)count * 8)) return 0;
    unsigned char *action = table[index];
    if (!mts_readable(action, 0x58) ||
        *(uintptr_t *)action != b + (index == 30 ? camera_action_30_vtable_rva : camera_action_31_vtable_rva) ||
        *(void **)(action + camera_action_camera_offset) != camera ||
        *(void **)(action + camera_action_controller_offset) != ctl ||
        *(void **)(action + camera_action_view_offset) != view || !hunter_camera_book(actor, b)) return 0;
    uint32_t clip = *(uint32_t *)(p + motion_clip_index_offset);
    if (!(*(uint32_t *)(p + motion_clip_flags_offset) & 0x10u) ||
        !(clip == 40 || clip == 41 || clip == 42 || clip == 43 || clip == 44 || clip == 46 || clip == 48) ||
        *(uint32_t *)(p + motion_rate_offset) != 0x3f800000u) return 0;
    unsigned char *resource = *(unsigned char **)(p + motion_clip_resource_offset);
    if (!mts_readable(resource, 0xb0)) return 0;
    unsigned char *clips = *(unsigned char **)(resource + resource_clip_table_offset);
    if (!mts_readable(clips, 8)) return 0;
    uint16_t clip_count = *(uint16_t *)(clips + clip_table_count_offset);
    if (clip >= clip_count || !mts_readable(clips + clip_table_entries_offset, (SIZE_T)clip_count * 8)) return 0;
    unsigned char *entry = *(unsigned char **)(clips + clip_table_entries_offset + (SIZE_T)clip * 8);
    if (!mts_readable(entry, 0x24)) return 0;
    /* Motion tracks a fractional frame. The clip entry stores a signed frame count. */
    union { float f; uint32_t u; } frame;
    frame.f = *(float *)(p + motion_current_frame_offset);
    int32_t duration = *(int32_t *)(entry + clip_duration_frames_offset);
    return (frame.u & 0x7f800000u) != 0x7f800000u && frame.f >= 0.0f &&
        duration > 1 && frame.f < (float)(duration - 1) && !mts_bypass_held();
}

static void hunter_camera_tick(void *motion)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    int changed = 0;
    __try {
        if (hunter_camera_eligible(motion, ret))
            changed = InterlockedCompareExchange((volatile LONG *)((unsigned char *)motion + motion_rate_offset),
                                                 0x40c00000, 0x3f800000) == 0x3f800000;
    } __except(EXCEPTION_EXECUTE_HANDLER) { changed = 0; }
    __try { hunter_camera_original_tick(motion); }
    __finally {
        if (changed) {
            __try {
                if (mts_writable((unsigned char *)motion + motion_rate_offset, 4))
                    InterlockedCompareExchange((volatile LONG *)((unsigned char *)motion + motion_rate_offset),
                                               0x3f800000, 0x40c00000);
            } __except(EXCEPTION_EXECUTE_HANDLER) { }
        }
    }
}

static int hunter_camera_supported(uintptr_t b)
{
    enum {
        motion_tick_slot_offset = 0x48,
        camera_action_setup_slot_offset = 0x30,
        camera_action_tick_slot_offset = 0x38,
        camera_action_30_setup_rva = 0x12719a0u,
        camera_action_30_tick_rva = 0x1283170u,
        camera_action_31_tick_rva = 0x1283290u
    };
    if (!mts_readable((void *)(b + camera_motion_vtable_rva + motion_tick_slot_offset), 8) ||
        !mts_readable((void *)(b + camera_action_30_vtable_rva + camera_action_setup_slot_offset), 16) ||
        !mts_readable((void *)(b + camera_action_31_vtable_rva + camera_action_tick_slot_offset), 8)) return 0;
    if (*(uintptr_t *)(b + camera_motion_vtable_rva + motion_tick_slot_offset) != b + 0x1fbeaf0u ||
        *(uintptr_t *)(b + camera_action_30_vtable_rva + camera_action_setup_slot_offset) != b + camera_action_30_setup_rva ||
        *(uintptr_t *)(b + camera_action_30_vtable_rva + camera_action_tick_slot_offset) != b + camera_action_30_tick_rva ||
        *(uintptr_t *)(b + camera_action_31_vtable_rva + camera_action_tick_slot_offset) != b + camera_action_31_tick_rva) return 0;
    return mts_region_hash(b, camera_motion_tick_rva, 0x38au, UINT64_C(0x460bf0d4b8a1b64a)) &&
        mts_region_hash(b, 0x1fbeb08u, 5u, UINT64_C(0xfc332703d30269cd)) &&
        mts_region_hash(b, 0x1fb1399u, 5u, UINT64_C(0xa69000c2593d4072)) &&
        mts_region_hash(b, camera_action_30_setup_rva, 0x221u, UINT64_C(0xef441245ea971671)) &&
        mts_region_hash(b, camera_action_30_tick_rva, 0x112u, UINT64_C(0x14145cd4790448a6)) &&
        mts_region_hash(b, camera_action_31_tick_rva, 0xbcu, UINT64_C(0xb5f3631f89233414)) &&
        mts_region_hash(b, MTS_LOCAL_PLAYER_GETTER_RVA, MTS_LOCAL_PLAYER_GETTER_SIZE, MTS_LOCAL_PLAYER_GETTER_HASH);
}

void hunter_camera_install(uintptr_t b)
{
    void *target = (void *)(b + camera_motion_tick_rva);
    __try {
        if (!hunter_camera_supported(b)) return;
        if (MH_CreateHook(target, (void *)hunter_camera_tick, (void **)&hunter_camera_original_tick) != MH_OK) return;
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            return;
        }
        InterlockedExchange(&hunter_camera_active, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
}
