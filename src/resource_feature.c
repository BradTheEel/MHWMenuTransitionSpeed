#include "mts_internal.h"
#include "mts_build_15539686.h"

/* Optional Resource Center feature. Native button callers retain dispatch and bookkeeping. */
typedef uint32_t (*MtsButtonPoll)(void *, int32_t *, uint8_t *, uint8_t *, uint32_t *);
static MtsButtonPoll mts_original_button_poll;
static volatile LONG mts_resource_active;

enum {
    resource_button_vtable_rva = 0x30cc090u,
    resource_button_poll_rva = 0xb87410u,
    resource_confirm_callback_rva = 0x10dc090u,
    resource_button_confirm_rva = 0xb86d50u,
    resource_button_confirm_slot_offset = 0x108u
};

int mts_resource_is_active(void)
{
    return InterlockedCompareExchange(&mts_resource_active, 0, 0);
}

static uint32_t mts_button_poll_detour(void *button, int32_t *special, uint8_t *accept,
                                      uint8_t *cancel, uint32_t *action)
{
    enum {
        button_ui_owner_offset = 0x80,
        button_callback_receiver_offset = 0x120,
        button_callback_offset = 0x128,
        button_armed_offset = 0x171,
        button_flag_172_offset = 0x172,
        button_blocked_offset = 0x173,
        button_flag_174_offset = 0x174,
        button_confirm_enabled_offset = 0x177,
        button_flag_179_offset = 0x179,
        button_flag_17a_offset = 0x17a,
        owner_confirm_button_offset = 0x28f0,
        owner_input_gate_1_offset = 0x22c1,
        owner_input_gate_2_offset = 0x22c2,
        owner_input_flags_offset = 0x229c,
        owner_close_flags_offset = 0x2298,
        owner_current_lifecycle_offset = 0x1d00,
        owner_next_lifecycle_offset = 0x1d04,
        owner_completed_order_offset = 0x2cf4,
        owner_mode_offset = 0x2cf0
    };
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    uint32_t result = mts_original_button_poll(button, special, accept, cancel, action);
    __try {
        unsigned char *b = (unsigned char *)button;
        if (!mts_resource_is_active() ||
            (ret != mts_game_base + 0xb870d5u && ret != mts_game_base + 0xb87335u) ||
            result != 0 || !mts_readable(b, 0x180)) return result;
        unsigned char *o = *(unsigned char **)(b + button_ui_owner_offset);
        if (!mts_readable(o, 0x2cf8) || *(uintptr_t *)b != mts_game_base + resource_button_vtable_rva ||
            *(uintptr_t *)o != mts_game_base + MTS_RESOURCE_VTABLE_RVA ||
            *(void **)(o + owner_confirm_button_offset) != button ||
            *(void **)(b + button_callback_receiver_offset) != o ||
            *(uintptr_t *)(b + button_callback_offset) != mts_game_base + resource_confirm_callback_rva ||
            !mts_readable((void *)(mts_game_base + resource_button_vtable_rva + resource_button_confirm_slot_offset), 8) ||
            *(uintptr_t *)(mts_game_base + resource_button_vtable_rva + resource_button_confirm_slot_offset) !=
                mts_game_base + resource_button_confirm_rva ||
            !b[button_armed_offset] || !b[button_confirm_enabled_offset] || b[button_blocked_offset] ||
            (b[button_flag_174_offset] && b[button_flag_172_offset]) ||
            (b[button_flag_179_offset] && !b[button_flag_17a_offset]) ||
            (!o[owner_input_gate_1_offset] && !o[owner_input_gate_2_offset]) ||
            (*(uint32_t *)(o + owner_input_flags_offset) & 0x400u) ||
            (*(uint32_t *)(o + owner_close_flags_offset) & 0x20u) ||
            *(uint32_t *)(o + owner_current_lifecycle_offset) != 2u ||
            *(uint32_t *)(o + owner_next_lifecycle_offset) != 2u ||
            *(int32_t *)(o + owner_completed_order_offset) != -1 ||
            *(uint32_t *)(o + owner_mode_offset) > 2u) return result;
        if (mts_bypass_held() || !mts_readable(special, 4) ||
            !mts_writable(accept, 1) || !mts_readable(cancel, 1) || !mts_readable(action, 4) ||
            *special != -1 || *accept || *cancel || *action != 9) return result;
        *accept = 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Preserve native outputs on uncertainty. */ }
    return result;
}

static int mts_resource_supported(uintptr_t b)
{
    enum {
        resource_tick_slot_offset = 0x48u,
        button_input_slot_offset = 0xe8u
    };
    if (!mts_readable((void *)(b + MTS_RESOURCE_VTABLE_RVA + resource_tick_slot_offset), 8) ||
        !mts_readable((void *)(b + resource_button_vtable_rva + button_input_slot_offset), 0x28)) return 0;
    if (*(uintptr_t *)(b + MTS_RESOURCE_VTABLE_RVA + resource_tick_slot_offset) != b + MTS_COMMON_TICK_RVA ||
        *(uintptr_t *)(b + resource_button_vtable_rva + button_input_slot_offset) != b + 0xb76fb0u ||
        *(uintptr_t *)(b + resource_button_vtable_rva + resource_button_confirm_slot_offset) !=
            b + resource_button_confirm_rva) return 0;
    return mts_region_hash(b, 0xb87040u, 0x172u, UINT64_C(0x86a4ec7a1cfb6aef)) &&
        mts_region_hash(b, 0xb872a0u, 0x170u, UINT64_C(0x8ce99399266c5935)) &&
        mts_region_hash(b, resource_button_poll_rva, 0x45du, UINT64_C(0xe8ed20cbc46fc9e5)) &&
        mts_region_hash(b, resource_confirm_callback_rva, 0x10eu, UINT64_C(0x61b400438f62d0ee)) &&
        mts_region_hash(b, 0x10dc230u, 0x184u, UINT64_C(0x9e4e646878def6e0)) &&
        mts_region_hash(b, resource_button_confirm_rva, 0x58u, UINT64_C(0x3678782dfaf5f3c0)) &&
        mts_region_hash(b, 0x1fb7cc0u, 0x25u, UINT64_C(0x75fe309995471dad)) &&
        mts_region_hash(b, 0x10dc4e0u, 0x2c4u, UINT64_C(0x3f9c112755696a61)) &&
        mts_region_hash(b, 0x1fb55e0u, 0x1c5u, UINT64_C(0xd263ec82abf090c7));
}

void mts_resource_install(uintptr_t base)
{
    void *target = (void *)(base + resource_button_poll_rva);
    __try {
        if (!mts_resource_supported(base)) return;
        if (MH_CreateHook(target, (void *)mts_button_poll_detour, (void **)&mts_original_button_poll) != MH_OK) return;
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            return;
        }
        InterlockedExchange(&mts_resource_active, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Optional feature stays disabled. */ }
}
