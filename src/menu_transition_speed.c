/* MenuTransitionSpeed 0.1.15 — experimental native UI animation delta hook.
 * Target: Monster Hunter: World build 15539686 only.
 * This module adjusts the delta passed to the game's native animation updater;
 * it never changes lifecycle state or bypasses native callbacks.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#ifndef MTS_TEST_BUILD
#include "mts_build_15539686.h"
#include "mts_fade_rules.h"
#include "mts_internal.h"
#endif

#define MTS_VERSION "0.1.15"
#define MTS_ANIMATION_UPDATE_RVA 0x02354370u
#define MTS_STAGE_RVA 0x00B76A70u
#define MTS_TOP_RVA 0x0348AB38u
#define MTS_LIST_RVA 0x030CB560u
#define MTS_LIFECYCLE_STATE_OFFSET 0x1D04u
#define MTS_MAX_NATIVE_DELTA 2.0f
#define MTS_MULTIPLIER 16.0f
#define MTS_RESOURCE_MULTIPLIER 4.0f

typedef struct MtsTarget {
    uint32_t vtable_rva;
    const char *name;
} MtsTarget;

static const MtsTarget mts_targets[] = {
    {0x03481908u, "uGUIStartMenu"},
    {0x0346C578u, "uGUIEquipCraftShop"},
    {0x03469B50u, "uGUIEquipArmorShop"},
    {0x02FA7DA0u, "uGUIEquipWeaponUpgrade"},
    {0x0346AAA8u, "uGUIEquipArmorUpgrade"},
    {0x0348AB38u, "uGUITopMenuList"},
};

static int mts_delta_eligible(float dt, uint32_t state, uint32_t shift) {
    union { float f; uint32_t u; } value;
    value.f=dt;
    return (value.u & 0x7f800000u)!=0x7f800000u && !(value.u & 0x80000000u) &&
        (value.u & 0x7fffffffu)!=0 && dt<=MTS_MAX_NATIVE_DELTA &&
        (state==1u || state==3u) && !shift;
}

/* Pure policy entrypoint shared by the production detour and private tests. */
__declspec(dllexport) float __cdecl MTS_ApplyDelta(
    float native_dt,
    uint32_t lifecycle_state,
    uint32_t target_vtable_rva,
    uint32_t shift_down)
{
    if (!mts_delta_eligible(native_dt, lifecycle_state, shift_down)) return native_dt;

    for (unsigned i = 0; i < sizeof(mts_targets) / sizeof(mts_targets[0]); ++i) {
        if (mts_targets[i].vtable_rva == target_vtable_rva &&
            1)
            return native_dt * MTS_MULTIPLIER;
    }
    return native_dt;
}

__declspec(dllexport) float __cdecl MTS_ApplyDeltaForTransition(float dt, uint32_t state, uint32_t vtable, uint32_t shift, uint32_t active_list)
{
    if (state == 2u && vtable == MTS_TOP_RVA && active_list)
        return MTS_ApplyDelta(dt, 1u, vtable, shift);
    return MTS_ApplyDelta(dt, state, vtable, shift);
}

__declspec(dllexport) float __cdecl MTS_AdjustCountdown(float countdown, float clock, float interval,
    uint32_t mode, uint32_t owner_valid, uint32_t member, uint32_t active, uint32_t shift)
{
    union { float f; uint32_t u; } c, d;
    c.f = countdown; d.f = clock;
    if (mode != 1u || !owner_valid || !member || !active || shift || interval != 4.0f ||
        (c.u & 0x7f800000u) == 0x7f800000u || countdown < 0.0f || countdown > 100.0f ||
        (d.u & 0x7f800000u) == 0x7f800000u || clock <= 0.0f || clock > 2.0f)
        return countdown;
    float adjusted = countdown - clock * (MTS_MULTIPLIER - 1.0f);
    return adjusted < 0.0f ? 0.0f : adjusted;
}

#ifdef MTS_TEST_BUILD
typedef void (__cdecl *MtsTestOriginal)(float);

/* Test-only adapter. Test builds omit DllMain hook installation entirely. */
__declspec(dllexport) void __cdecl MTS_TestDispatch(
    float native_dt,
    uint32_t lifecycle_state,
    uint32_t target_vtable_rva,
    uint32_t shift_down,
    MtsTestOriginal original)
{
    if (original)
        original(MTS_ApplyDelta(native_dt, lifecycle_state,
                                target_vtable_rva, shift_down));
}
#else

typedef void (*MtsAnimationUpdate)(void *ui, float dt);
static MtsAnimationUpdate mts_original_update;
uintptr_t mts_game_base;
static volatile LONG mts_installed;
static void (*mts_original_stage)(void *);

/* VirtualQuery bounds every speculative read; a malformed tree fails closed. */
int mts_readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION m;
    uintptr_t a = (uintptr_t)p;
    if (!p || a + n < a || !VirtualQuery(p, &m, sizeof(m))) return 0;
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    return a + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}
int mts_writable(void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!mts_readable(p, n) || !VirtualQuery(p, &m, sizeof(m))) return 0;
    DWORD prot = m.Protect & 0xffu;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}
static int mts_reachable(void *owner, void *target)
{
    unsigned char *o = (unsigned char *)owner;
    if (!mts_readable(o + 0x298, sizeof(void *))) return 0;
    unsigned char *root = *(unsigned char **)(o + 0x298);
    if (!mts_readable(root, 0x70)) return 0;
    unsigned char *stack[256]; unsigned n = 0, visited = 0;
    stack[n++] = root;
    while (n && visited++ < 256) {
        unsigned char *node = stack[--n];
        if (!node) continue;
        if (!mts_readable(node, 0x78)) return 0;
        uint32_t status = *(uint32_t *)(node + 0x58);
        if (n + 2 > 256) return 0;
        stack[n++] = *(unsigned char **)(node + 0x70);
        if ((status & 0x0100000au) == 0x0100000au) {
            if (node == target && (status & 0x12u) == 0x12u) return 1;
            stack[n++] = *(unsigned char **)(node + 0x68);
        }
    }
    return 0;
}
static int mts_list_qualified(void *owner, void *list)
{
    unsigned char *o = (unsigned char *)owner, *l = (unsigned char *)list;
    if (!mts_game_base || !mts_readable(o, 0x2910) || !mts_readable(l, 0x280)) return 0;
    if (*(uintptr_t *)o != mts_game_base + MTS_TOP_RVA ||
        *(uintptr_t *)l != mts_game_base + MTS_LIST_RVA ||
        *(void **)(l + 0x80) != owner ||
        (list != *(void **)(o + 0x28f8) && list != *(void **)(o + 0x2908)) ||
        *(uint32_t *)(l + 0x228) != 1u || *(float *)(l + 0x23c) != 4.0f ||
        (*(uint32_t *)(o + MTS_LIFECYCLE_STATE_OFFSET) != 1u &&
         *(uint32_t *)(o + MTS_LIFECYCLE_STATE_OFFSET) != 2u)) return 0;
    return mts_reachable(owner, list);
}
/* A positive native row blocker is evidence that visible staging is still
 * in progress. Read every eligible row: uncertainty disables acceleration. */
static int mts_pending_rows(void *list)
{
    unsigned char *l = (unsigned char *)list;
    if (!mts_readable(l, 0x280)) return 0;
    uint32_t count = *(uint32_t *)(l + 0x1d8);
    unsigned char **rows = *(unsigned char ***)(l + 0x1e8);
    if (!count || count > 64 || !mts_readable(rows, (SIZE_T)count * sizeof(*rows))) return 0;
    int pending = 0;
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char *row = rows[i];
        if (!mts_readable(row, 0x61c)) return 0;
        if (!*(uint32_t *)(row + 0x618) || !(row[0x5b] & 1u)) continue;
        uintptr_t vmt = *(uintptr_t *)row;
        if ((vmt != mts_game_base + 0x30ca4d0u &&
             vmt != mts_game_base + 0x30cbc90u &&
             vmt != mts_game_base + 0x35171d0u) ||
            !mts_readable((void *)vmt, 0x60) ||
            *(uintptr_t *)(vmt + 0x58) != mts_game_base + 0x234dac0u) return 0;
        unsigned char *ctl = *(unsigned char **)(row + 0x1c0);
        if (!ctl) continue; /* native getter returns ready */
        union { float f; uint32_t u; } elapsed, threshold;
        elapsed.f = *(float *)(row + 0x1a0); threshold.f = *(float *)(row + 0x1a4);
        if ((elapsed.u & 0x7f800000u) == 0x7f800000u ||
            (threshold.u & 0x7f800000u) == 0x7f800000u) return 0;
        float limit = threshold.f;
        if (limit < 0.0f) {
            if (!mts_readable(ctl, 0xc0)) return 0;
            unsigned char *nested = *(unsigned char **)(ctl + 0xb8);
            if (nested && !mts_readable(nested, 8)) return 0;
            limit = nested ? (float)*(uint32_t *)(nested + 4) : 0.0f;
        }
        if (elapsed.f < limit) pending = 1;
    }
    return pending;
}
static int mts_active_intro(void *owner)
{
    unsigned char *o = (unsigned char *)owner;
    if (!mts_readable(o, 0x2910)) return 0;
    void *a = *(void **)(o + 0x28f8), *b = *(void **)(o + 0x2908);
    return (mts_list_qualified(owner, a) && mts_pending_rows(a)) ||
           (mts_list_qualified(owner, b) && mts_pending_rows(b));
}
static void mts_stage_detour(void *list)
{
    unsigned char *l = (unsigned char *)list;
    __try {
    if (InterlockedCompareExchange(&mts_installed, 0, 0) && mts_readable(l, 0x280)) {
        void *owner = *(void **)(l + 0x80);
        if (mts_list_qualified(owner, list) && mts_pending_rows(list) && mts_readable(owner, 0x70)) {
            float *countdown = (float *)(l + 0x278);
            float before = *countdown;
            float after = MTS_AdjustCountdown(before, *(float *)((unsigned char *)owner + 0x6c),
                *(float *)(l + 0x23c), 1, 1, 1, 1, mts_bypass_held());
            union { float f; uint32_t u; } a, b;
            a.f = after; b.f = before;
            if (a.u != b.u && mts_writable(countdown, sizeof(*countdown))) *countdown = after;
        }
    }
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* fail closed; native still runs */ }
    mts_original_stage(list);
}


static const MtsTarget *mts_find_target(uint32_t vtable_rva, unsigned *index)
{
    for (unsigned i = 0; i < sizeof(mts_targets) / sizeof(mts_targets[0]); ++i) {
        if (mts_targets[i].vtable_rva == vtable_rva) {
            if (index) *index = i;
            return &mts_targets[i];
        }
    }
    return 0;
}

static void mts_update_detour(void *ui, float native_dt)
{
    float adjusted_dt = native_dt;
    __try {
    if (InterlockedCompareExchange(&mts_installed, 0, 0) && ui && mts_game_base && mts_readable(ui, MTS_LIFECYCLE_STATE_OFFSET + 4)) {
        uintptr_t vtable = *(const uintptr_t *)ui;
        if (vtable >= mts_game_base && vtable - mts_game_base < MTS_IMAGE_SIZE) {
            uint32_t vtable_rva = (uint32_t)(vtable - mts_game_base);
            const MtsTarget *target = mts_find_target(vtable_rva, 0);
            if (target || (vtable_rva == MTS_RESOURCE_VTABLE_RVA && mts_resource_is_active()) ||
                ((vtable_rva == MTS_HUNTER_NOTE_UI_VTABLE_RVA || vtable_rva == MTS_HUNTER_NOTE_RIGHT_VTABLE_RVA) &&
                 hunter_note_is_active())) {
                uint32_t state = *(const volatile uint32_t *)
                    ((const unsigned char *)ui + MTS_LIFECYCLE_STATE_OFFSET);
                if (state == 1u || state == 3u || (state == 2u && vtable_rva == MTS_TOP_RVA && mts_active_intro(ui))) {
                    int bypass = mts_bypass_held();
                    if (vtable_rva == MTS_HUNTER_NOTE_UI_VTABLE_RVA || vtable_rva == MTS_HUNTER_NOTE_RIGHT_VTABLE_RVA) {
                        if (state == 1u && (vtable_rva != MTS_HUNTER_NOTE_UI_VTABLE_RVA || (mts_readable((unsigned char *)ui + 0x2e50, 4) && *(uint32_t *)((unsigned char *)ui + 0x2e50) == 0u)) && mts_delta_eligible(native_dt, state, bypass))
                            adjusted_dt = native_dt * 6.0f;
                    } else if (vtable_rva == MTS_RESOURCE_VTABLE_RVA) {
                        if (mts_delta_eligible(native_dt, state, bypass))
                            adjusted_dt = native_dt * MTS_RESOURCE_MULTIPLIER;
                    } else {
                        adjusted_dt = MTS_ApplyDeltaForTransition(native_dt, state, vtable_rva,
                                                     bypass, 1u);
                    }

                }
            }
        }
    }

    } __except(EXCEPTION_EXECUTE_HANDLER) { adjusted_dt = native_dt; }
    /* MinHook trampoline preserves the native writer and its callback order. */
    mts_original_update(ui, adjusted_dt);
}

static int mts_bytes_equal(const unsigned char *actual,
                           const unsigned char *expected, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (actual[i] != expected[i]) return 0;
    return 1;
}


typedef uintptr_t (*MtsFadeSetup)(void *, float, float, float, uint32_t, uint32_t);
typedef uintptr_t (*MtsContextMethod)(void *);
typedef uintptr_t (*MtsContextMethod2)(void *, uint32_t);
typedef uintptr_t (*MtsContextMethod3)(void *, void *, void *);
static MtsFadeSetup mts_original_fade;
static MtsContextMethod mts_original_selection, mts_original_armor_callback, mts_original_craft_callback, mts_original_box_callback;
static MtsContextMethod3 mts_original_palico_callback, mts_original_pigment_entry, mts_original_pigment_exit, mts_original_box_pigment_open, mts_original_box_layer_open;
static MtsContextMethod mts_original_box_change_return, mts_original_box_other_return;
static MtsContextMethod2 mts_original_box_dispatch;
typedef struct MtsFadeFrame {
    struct MtsFadeFrame *previous;
    void *owner;
    uintptr_t entry, subentry;
    int32_t category, group, row;
    MtsFadeContext kind;
    uint32_t selector;
} MtsFadeFrame;
static __declspec(thread) MtsFadeFrame *mts_fade_frame;

static int mts_top_valid(void *owner) {
    return mts_game_base && mts_readable(owner, 0x297c) &&
        *(uintptr_t *)owner == mts_game_base + MTS_TOP_RVA;
}
static int mts_selection_snapshot(void *owner, MtsFadeFrame *f) {
    unsigned char *o = (unsigned char *)owner;
    if (!mts_top_valid(owner)) return 0;
    int32_t category=*(int32_t *)(o+0x2968), group=*(int32_t *)(o+0x2974), row=*(int32_t *)(o+0x2978);
    if (category<0 || category>63 || group<0 || group>1023 || row<0 || row>1023) return 0;
    uintptr_t *table=(uintptr_t *)(mts_game_base+0x348a748u+(uintptr_t)category*24u);
    if (!mts_readable(table,sizeof(*table)) || !*table) return 0;
    uintptr_t entry=*table+(uintptr_t)group*32u;
    if (entry<*table || !mts_readable((void *)entry,16)) return 0;
    uintptr_t sub=*(uintptr_t *)(entry+8);
    uintptr_t sel=sub+(uintptr_t)row*32u;
    if (!sub || sel<sub || !mts_readable((void *)sel,4)) return 0;
    uint32_t selector=*(uint32_t *)sel;
    if (mts_fade_context_from_selector(selector) == MTS_FADE_CONTEXT_NONE) return 0;
    f->selector=selector;
    f->owner=owner; f->entry=entry; f->subentry=sub; f->category=category; f->group=group; f->row=row;
    return 1;
}
static int mts_same_selection(MtsFadeFrame *f) {
    MtsFadeFrame now={0};
    return mts_selection_snapshot(f->owner,&now) && now.entry==f->entry && now.subentry==f->subentry &&
        now.category==f->category && now.group==f->group && now.row==f->row && now.selector==f->selector;
}
static uintptr_t mts_selection_detour(void *owner) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_selection_snapshot(owner, &frame))
            frame.kind = mts_fade_context_from_selector(frame.selector);
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind = MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame;
    uintptr_t result;
    __try { result=mts_original_selection(owner); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_callback_common(void *object, uint32_t expected, MtsFadeContext kind, MtsContextMethod original) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_readable(object,16) && *(uintptr_t *)object==mts_game_base+expected) {
            frame.owner=*(void **)((unsigned char *)object+8);
            if (mts_top_valid(frame.owner)) frame.kind=kind;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame;
    uintptr_t result;
    __try { result=original(object); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_armor_callback_detour(void *object) {
    uint32_t v=0;
    __try { if (mts_readable(object,8)) { uintptr_t p=*(uintptr_t *)object; if (p>=mts_game_base && p-mts_game_base<MTS_IMAGE_SIZE) v=(uint32_t)(p-mts_game_base); } } __except(EXCEPTION_EXECUTE_HANDLER) { v=0; }
    if (v==0x348de20u) return mts_callback_common(object,v,MTS_FADE_CONTEXT_ARMOR_DE20,mts_original_armor_callback);
    if (v==0x348de80u) return mts_callback_common(object,v,MTS_FADE_CONTEXT_ARMOR_DE80,mts_original_armor_callback);
    if (v==0x348dd00u) return mts_callback_common(object,v,MTS_FADE_CONTEXT_ARMOR_DD00,mts_original_armor_callback);
    if (v==0x348e1b0u) return mts_callback_common(object,v,MTS_FADE_CONTEXT_ARMOR_E1B0,mts_original_armor_callback);
    return mts_callback_common(object,0x348dbe0u,MTS_FADE_CONTEXT_ARMOR_DEFAULT,mts_original_armor_callback);
}
static uintptr_t mts_craft_callback_detour(void *object) {
    return mts_callback_common(object, 0x348e330u, MTS_FADE_CONTEXT_CRAFT_CALLBACK, mts_original_craft_callback);
}
static uintptr_t mts_box_callback_detour(void *object) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_readable(object,16) && *(uintptr_t *)object==mts_game_base+0x346beb0u) {
            frame.owner=*(void **)((unsigned char *)object+8);
            if (mts_readable(frame.owner,8) && *(uintptr_t *)frame.owner==mts_game_base+0x346b020u) frame.kind=MTS_FADE_CONTEXT_BOX_CALLBACK;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame; uintptr_t result;
    __try { result=mts_original_box_callback(object); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_palico_callback_detour(void *object, void *arg2, void *arg3) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_readable(object,16) && *(uintptr_t *)object==mts_game_base+0x348e270u) {
            frame.owner=*(void **)((unsigned char *)object+8);
            if (mts_top_valid(frame.owner)) frame.kind=MTS_FADE_CONTEXT_PALICO_CALLBACK;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame; uintptr_t result;
    __try { result=mts_original_palico_callback(object,arg2,arg3); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_pigment_common(void *object, void *arg2, void *arg3, uint32_t expected, MtsFadeContext kind, MtsContextMethod3 original) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_readable(object,16) && *(uintptr_t *)object==mts_game_base+expected) {
            frame.owner=*(void **)((unsigned char *)object+8);
            if (mts_readable(frame.owner,8) && *(uintptr_t *)frame.owner==mts_game_base+0x3469b50u) frame.kind=kind;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame; uintptr_t result;
    __try { result=original(object,arg2,arg3); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_pigment_entry_detour(void *o, void *a, void *b) {
    return mts_pigment_common(o, a, b, 0x346a6f8u, MTS_FADE_CONTEXT_PIGMENT_ENTRY, mts_original_pigment_entry);
}
static uintptr_t mts_pigment_exit_detour(void *o, void *a, void *b) {
    return mts_pigment_common(o, a, b, 0x346a6c8u, MTS_FADE_CONTEXT_PIGMENT_EXIT, mts_original_pigment_exit);
}
static uintptr_t mts_box_dispatch_detour(void *owner, uint32_t event) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try { if (event==0x4bu && mts_top_valid(owner)) { frame.owner=owner; frame.kind=MTS_FADE_CONTEXT_BOX_DISPATCH; } }
    __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame; uintptr_t result;
    __try { result=mts_original_box_dispatch(owner,event); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_box_change_return_detour(void *o) {
    return mts_callback_common(o, 0x348ba80u, MTS_FADE_CONTEXT_BOX_CHANGE_RETURN, mts_original_box_change_return);
}
static uintptr_t mts_box_other_return_detour(void *o) {
    uint32_t v=0;
    __try { if (mts_readable(o,8)) { uintptr_t p=*(uintptr_t *)o; if (p>=mts_game_base && p-mts_game_base<MTS_IMAGE_SIZE) v=(uint32_t)(p-mts_game_base); } }
    __except(EXCEPTION_EXECUTE_HANDLER) { v=0; }
    if (v==0x348bd80u) return mts_callback_common(o,v,MTS_FADE_CONTEXT_BOX_OTHER_RETURN_BD80,mts_original_box_other_return);
    return mts_callback_common(o,0x348bd20u,MTS_FADE_CONTEXT_BOX_OTHER_RETURN,mts_original_box_other_return);
}
static uintptr_t mts_box_open_common(void *o, void *a, void *b, uint32_t expected, MtsFadeContext kind, MtsContextMethod3 original) {
    MtsFadeFrame frame={0}; frame.previous=mts_fade_frame;
    __try {
        if (mts_readable(o,16) && *(uintptr_t *)o==mts_game_base+expected) {
            frame.owner=*(void **)((unsigned char *)o+8);
            if (mts_top_valid(frame.owner)) frame.kind=kind;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { frame.kind=MTS_FADE_CONTEXT_NONE; }
    mts_fade_frame=&frame; uintptr_t result;
    __try { result=original(o,a,b); }
    __finally { mts_fade_frame=frame.previous; }
    return result;
}
static uintptr_t mts_box_pigment_open_detour(void *o, void *a, void *b) {
    return mts_box_open_common(o, a, b, 0x348bcf0u, MTS_FADE_CONTEXT_BOX_PIGMENT_OPEN, mts_original_box_pigment_open);
}
static uintptr_t mts_box_layer_open_detour(void *o, void *a, void *b) {
    return mts_box_open_common(o, a, b, 0x348bd50u, MTS_FADE_CONTEXT_BOX_LAYER_OPEN, mts_original_box_layer_open);
}
static uintptr_t mts_fade_detour(void *component, float out, float in, float hold, uint32_t a, uint32_t b) {
    uintptr_t ret=(uintptr_t)__builtin_return_address(0);
    union { float f; uint32_t u; } x={.f=out}, y={.f=in}, z={.f=hold};
    __try {
        MtsFadeContext context = mts_fade_frame ? mts_fade_frame->kind : MTS_FADE_CONTEXT_NONE;
        if (mts_fade_context_uses_selection(context) && !mts_same_selection(mts_fade_frame))
            context = MTS_FADE_CONTEXT_NONE;
        uintptr_t *cell=(uintptr_t *)(mts_game_base+0x500ded0u);
        int valid=mts_game_base && mts_readable(cell,8) && *cell && mts_readable((void *)*cell,8) &&
            *(uintptr_t *)*cell==mts_game_base+0x33f4f70u && (uintptr_t)component==*cell+0x1e8u;
        uint32_t caller=(ret>=mts_game_base && ret-mts_game_base<MTS_IMAGE_SIZE) ? (uint32_t)(ret-mts_game_base) : 0;
        const MtsFadeRule *rule = mts_fade_rule_for_caller(caller);
        int context_matches = mts_fade_rule_matches_context(rule, context);
        if (InterlockedCompareExchange(&mts_installed, 0, 0) &&
            mts_fade_shape_matches(rule, context_matches, x.u, y.u, z.u) && a == 0u && b == 0u) {
            int bypass = mts_bypass_held();
            if (!bypass && valid && context_matches)
                mts_fade_apply(rule, &out, &in, &hold);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Preserve original arguments on uncertainty. */ }
    return mts_original_fade(component,out,in,hold,a,b);
}

int mts_region_hash(uintptr_t base, uint32_t rva, uint32_t size, uint64_t expected) {
    if (!size || rva >= MTS_IMAGE_SIZE || size > MTS_IMAGE_SIZE-rva || !mts_readable((void *)(base+rva),size)) return 0;
    const unsigned char *p=(const unsigned char *)(base+rva); uint64_t h=UINT64_C(14695981039346656037);
    for (uint32_t i=0;i<size;i++) { h^=p[i]; h*=UINT64_C(1099511628211); }
    return h==expected;
}

static int mts_supported_image(uintptr_t base)
{
    if (!base) return 0;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
        return 0;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + (uintptr_t)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp != MTS_PE_TIMESTAMP ||
        nt->OptionalHeader.SizeOfImage != MTS_IMAGE_SIZE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;

    if (!mts_region_hash(base,0x1f20c00u,0x1e13u,UINT64_C(0x555f473b127b58f5))) return 0;
    if (!mts_region_hash(base,0x1f22a20u,0xb9u,UINT64_C(0x9b9b731acc634031))) return 0;
    if (!mts_region_hash(base,0x1f20820u,0x3d3u,UINT64_C(0xe85ed1e4ad127826))) return 0;
    if (!mts_region_hash(base,0x1f08cb0u,0x71u,UINT64_C(0x38bca0f4c53e7816))) return 0;
    if (!mts_region_hash(base,0x1dd8f30u,0x71u,UINT64_C(0x36cabfdbae902093))) return 0;
    if (!mts_region_hash(base,0x1fe790u,0x4cu,UINT64_C(0xcaa77670eb2e47d4))) return 0;
    if (!mts_region_hash(base,0x1fe940u,0x4cu,UINT64_C(0xc7ab4c966d6cc315))) return 0;

    if (!mts_region_hash(base,0x1ddef80u,0x1c96u,UINT64_C(0x45f78860692775f8))) return 0;
    if (!mts_region_hash(base,0x1de0c34u,0x100u,UINT64_C(0x38ea52f37e72fe1e))) return 0;
    if (!mts_region_hash(base,0x1ddd6a0u,0x54u,UINT64_C(0x987caf4dacdde223))) return 0;
    if (!mts_region_hash(base,0x1dd8eb0u,0x71u,UINT64_C(0x2e1ef5c7b1d71482))) return 0;
    if (!mts_region_hash(base,0x37a40u,0x4cu,UINT64_C(0xf63f386c5ecaa5a1))) return 0;
    if (*(uintptr_t *)(base+0x346beb0u+0x10u)!=base+0x1ddd6a0u ||
        *(uintptr_t *)(base+0x348de20u+0x10u)!=base+0x1f18170u ||
        *(uintptr_t *)(base+0x348de80u+0x10u)!=base+0x1f18170u) return 0;
    if (!mts_region_hash(base,0x1f14bd0u,0x200u,UINT64_C(0x0af4e0c00551753a))) return 0;
    if (!mts_region_hash(base,0x1f1a170u,0x0fu,UINT64_C(0x26e9c29e62d5259a))) return 0;
    if (!mts_region_hash(base,0x1dd9130u,0x71u,UINT64_C(0xe36f5cc7af1c6a8a))) return 0;
    if (!mts_region_hash(base,0x1feab0u,0x4cu,UINT64_C(0xba4fcd42403cbc3c))) return 0;
    if (!mts_region_hash(base,0x1f0c450u,0x1a0u,UINT64_C(0x43a5f4d9c3dbd833))) return 0;
    if (!mts_region_hash(base,0x1f0c9f0u,0x1e0u,UINT64_C(0x82a1c0e1f40d3a53))) return 0;
    if (*(uintptr_t *)(base+0x348dd00u+0x10u)!=base+0x1f18170u ||
        *(uintptr_t *)(base+0x348e1b0u+0x10u)!=base+0x1f18170u ||
        *(uintptr_t *)(base+0x348e270u+0x10u)!=base+0x1f1a170u) return 0;
    if (!mts_region_hash(base,0x1dc4c80u,0x0fu,UINT64_C(0x7af1fc192e0dcd38))) return 0;
    if (!mts_region_hash(base,0x1dc4d10u,0x0fu,UINT64_C(0x8bce91398d518cee))) return 0;
    if (!mts_region_hash(base,0x1dc1cf0u,0x2d7u,UINT64_C(0xffaf7776431c7d7c))) return 0;
    if (!mts_region_hash(base,0x1dc2b20u,0x23du,UINT64_C(0x60129ca9fc4079df))) return 0;
    if (!mts_region_hash(base,0x1dc8780u,0x269u,UINT64_C(0x1db384dda4bea10a))) return 0;
    if (!mts_region_hash(base,0x1dcc030u,0x300u,UINT64_C(0x14dfbba935c0a859))) return 0;
    if (*(uintptr_t *)(base+0x346a6f8u+0x10u)!=base+0x1dc4c80u ||
        *(uintptr_t *)(base+0x346a6c8u+0x10u)!=base+0x1dc4d10u) return 0;
    if (!mts_region_hash(base,0x1f276e0u,0x150du,UINT64_C(0xa561ea9014504b7c))) return 0;
    if (!mts_region_hash(base,0x1f182b0u,0x5bu,UINT64_C(0xedbd74b72adcc407))) return 0;
    if (!mts_region_hash(base,0x1f1a510u,0x5du,UINT64_C(0x20a6935a4f3d7594))) return 0;
    if (!mts_region_hash(base,0x1f1a180u,0x6au,UINT64_C(0x6aa5246522d7cc77))) return 0;
    if (!mts_region_hash(base,0x1f18e40u,0x70u,UINT64_C(0x0b3e194b5f78ed8f))) return 0;
    if (!mts_region_hash(base,0x523850u,0x71u,UINT64_C(0x5b9435c007b00ac0))) return 0;
    if (!mts_region_hash(base,0x1f08d30u,0x71u,UINT64_C(0xc78018738720808f))) return 0;
    if (!mts_region_hash(base,0x1f0c810u,0x180u,UINT64_C(0x8a3900cb39a32cde))) return 0;
    if (*(uintptr_t *)(base+0x348ba80u+0x10u)!=base+0x1f1a180u ||
        *(uintptr_t *)(base+0x348bcf0u+0x10u)!=base+0x1f182b0u ||
        *(uintptr_t *)(base+0x348bd50u+0x10u)!=base+0x1f1a510u ||
        *(uintptr_t *)(base+0x348bd20u+0x10u)!=base+0x1f18e40u ||
        *(uintptr_t *)(base+0x348bd80u+0x10u)!=base+0x1f18e40u) return 0;
    static const unsigned char row_ready_signature[] = {
        0x48,0x8b,0x81,0xc0,0x01,0x00,0x00,0x48,0x85,0xc0,0x74,0x19,
        0xf3,0x0f,0x10,0x89,0xa4,0x01,0x00,0x00,0x0f,0x57,0xc0,0x0f,
        0x2f,0xc8,0x72,0x0c,0x0f,0x2f,0x89,0xa0,0x01,0x00,0x00,0x77,
        0x29,0xb0,0x01,0xc3,0x48,0x8b,0x80,0xb8,0x00,0x00,0x00,0x48,
        0x85,0xc0,0x74,0x05,0x8b,0x50,0x04,0xeb,0x02,0x33,0xd2,0x8b,
        0xc2,0x0f,0x57,0xc0,0xf3,0x48,0x0f,0x2a,0xc0,0x0f,0x2f,0x81,
        0xa0,0x01,0x00,0x00,0x76,0xd7,0x32,0xc0,0xc3
    };
    if (!mts_bytes_equal((const unsigned char *)(base + 0x234dac0u), row_ready_signature, sizeof(row_ready_signature))) return 0;

    static const unsigned char update_signature[] = {
        0x40,0x57,0x48,0x83,0xEC,0x30,0xF6,0x81,0x70,0x03,0x00,0x00,
        0x01,0x48,0x8B,0xF9,0x0F,0x29,0x74,0x24,0x20,0x0F,0x28,0xF1
    };
    const unsigned char *update = (const unsigned char *)(base + MTS_ANIMATION_UPDATE_RVA);
    if (!mts_bytes_equal(update, update_signature, sizeof(update_signature))) return 0;

    static const unsigned char stage_signature[] = {
        0xf3,0x0f,0x10,0x91,0x3c,0x02,0x00,0x00,0x48,0x8b,0xd1,0x0f,
        0x2f,0x15,0xc6,0x93,0x2d,0x02,0x72,0x5d
    };
    if (!mts_bytes_equal((const unsigned char *)(base + MTS_STAGE_RVA), stage_signature, sizeof(stage_signature))) return 0;
    if (*(const uintptr_t *)(base + MTS_LIST_RVA + 0x70) != base + 0x00B77B90u) return 0;

    { static const unsigned char sig[] = {0x40,0x53,0x48,0x83,0xec,0x20,0xf3,0x0f,0x11,0x49,0x34,0x48,0x8b,0xd9,0xf3,0x0f,0x11,0x59,0x38,0xf3,0x0f,0x11,0x51,0x30,0xf3,0x0f,0x11,0x49,0x2c,0xc6,0x41,0x1c,0x01,0xc7,0x41,0x3c,0x00,0x00,0x00,0x00,0x8b,0x4c,0x24,0x50,0xe8,0x2f,0x87,0x1a,0x01,0x8b,0x4c,0x24,0x58,0x48,0x89,0x43,0x50,0xe8,0x22,0x87,0x1a,0x01,0x48,0x8b,0xcb,0x48,0x89,0x43,0x48,0xe8,0xd6,0xfc,0xff,0xff,0x66,0xc7,0x43,0x19,0x00,0x00,0x48,0x83,0xc4,0x20,0x5b,0xc3}; if (!mts_bytes_equal((const unsigned char *)(base+0xfcc550u),sig,sizeof(sig))) return 0; }
    { static const unsigned char sig[] = {0x4c,0x8b,0xdc,0x55,0x57,0x49,0x8d,0x6b,0xa1,0x48,0x81,0xec,0xf8,0x00,0x00,0x00,0x48,0x8b,0xf9,0x48,0x63,0x89,0x74,0x29,0x00,0x00,0x85,0xc9,0x0f,0x88,0xf1,0x1d,0x00,0x00,0x48,0x63,0x87,0x68,0x29,0x00,0x00,0x4c,0x8d,0x05,0xd0,0xf3,0x0d,0xfe,0x48,0x8d,0x14,0x40,0x48,0x8b,0xc1,0x48,0xc1,0xe0,0x05,0x49,0x03,0x84,0xd0,0x48,0xa7,0x48,0x03,0x0f,0x84,0xca,0x1d,0x00,0x00,0x48,0x63,0x8f,0x78,0x29,0x00,0x00,0x48,0x8b,0x40,0x08,0x48,0xc1,0xe1,0x05,0x49,0x89,0x5b,0x18,0x49,0x89,0x73,0xe8,0x8b,0x14,0x01}; if (!mts_bytes_equal((const unsigned char *)(base+0x1f20c00u),sig,sizeof(sig))) return 0; }
    { static const unsigned char sig[] = {0x48,0x83,0xec,0x38,0x48,0x8b,0x41,0x08,0xc7,0x80,0x04,0x2e,0x00,0x00,0x11,0x00,0x00,0x00,0x48,0x8b,0x49,0x08,0x48,0x8b,0x91,0x00,0x21,0x00,0x00,0x48,0x85,0xd2,0x74,0x06,0xf6,0x42,0x0c,0x0e,0x75,0x02,0x33,0xd2,0xe8,0x51,0xfb,0x09,0x00,0x48,0x8b,0x0d,0x2a,0x5d,0x0f,0x03,0xf3,0x0f,0x10,0x0d,0xa2,0xcd,0x4d,0x01,0x48,0x81,0xc1,0xe8,0x01,0x00,0x00,0xf3,0x0f,0x10,0x1d,0x7b,0xfb,0x08,0x01,0x0f,0x28,0xd1,0xc7,0x44,0x24,0x28,0x00,0x00,0x00,0x00,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xe8,0x7b,0x43,0x0b,0xff,0x48,0x83,0xc4,0x38,0xc3}; if (!mts_bytes_equal((const unsigned char *)(base+0x1f18170u),sig,sizeof(sig))) return 0; }
    { static const unsigned char sig[] = {0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x08,0x48,0x8b,0x91,0x00,0x21,0x00,0x00,0x48,0x85,0xd2,0x74,0x06,0xf6,0x42,0x0c,0x0e,0x75,0x02,0x33,0xd2,0xe8,0x0a,0xff,0x09,0x00,0x48,0x8b,0x43,0x08,0xf3,0x0f,0x10,0x1d,0x46,0xff,0x08,0x01,0xc7,0x44,0x24,0x28,0x00,0x00,0x00,0x00,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xc7,0x80,0x04,0x2e,0x00,0x00,0x11,0x00,0x00,0x00,0x48,0x8b,0x0d,0xbd,0x60,0x0f,0x03,0xf3,0x0f,0x10,0x0d,0x35,0xd1,0x4d,0x01,0x48,0x81,0xc1,0xe8,0x01,0x00,0x00,0x0f,0x28,0xd1,0xe8,0x26,0x47,0x0b,0xff,0x48,0x83,0xc4,0x30,0x5b,0xc3}; if (!mts_bytes_equal((const unsigned char *)(base+0x1f17dc0u),sig,sizeof(sig))) return 0; }
    { static const unsigned char sig[] = {0xf3,0x0f,0x10,0x0d,0xac,0x25,0x4d,0x01,0x48,0x8b,0x0d,0x25,0xb5,0x0e,0x03,0x0f,0x28,0xd1,0xf3,0x0f,0x10,0x1d,0x82,0x53,0x08,0x01,0x48,0x81,0xc1,0xe8,0x01,0x00,0x00,0x89,0x5c,0x24,0x28,0x89,0x5c,0x24,0x20,0xe8,0x86,0x9b,0x0a,0xff}; if (!mts_bytes_equal((const unsigned char *)(base+0x1f2299cu),sig,sizeof(sig))) return 0; }
    { static const unsigned char sig[] = {0x48,0x8b,0x0d,0x26,0xd3,0x0e,0x03,0xf3,0x0f,0x10,0x0d,0x9e,0x43,0x4d,0x01,0x48,0x81,0xc1,0xe8,0x01,0x00,0x00,0xf3,0x0f,0x10,0x1d,0x77,0x71,0x08,0x01,0x0f,0x28,0xd1,0x89,0x74,0x24,0x28,0x89,0x74,0x24,0x20,0xe8,0x7f,0xb9,0x0a,0xff}; if (!mts_bytes_equal((const unsigned char *)(base+0x1f20ba3u),sig,sizeof(sig))) return 0; }
    if (*(uintptr_t *)(base+0x348dbe0u+0x10u)!=base+0x1f18170u || *(uintptr_t *)(base+0x348e330u+0x10u)!=base+0x1f17dc0u) return 0;

    for (unsigned i = 0; i < sizeof(mts_targets) / sizeof(mts_targets[0]); ++i) {
        uint32_t rva = mts_targets[i].vtable_rva;
        if (rva + 0x50u > MTS_IMAGE_SIZE) return 0;
        const uintptr_t *slot = (const uintptr_t *)(base + rva + 0x48u);
        if (*slot != base + MTS_COMMON_TICK_RVA) return 0;
    }
    return 1;
}

typedef struct MtsHookSpec {
    uint32_t target_rva;
    LPVOID detour;
    LPVOID *original;
} MtsHookSpec;

static const MtsHookSpec mts_hooks[] = {
    {MTS_ANIMATION_UPDATE_RVA, (LPVOID)mts_update_detour, (LPVOID *)&mts_original_update},
    {MTS_STAGE_RVA, (LPVOID)mts_stage_detour, (LPVOID *)&mts_original_stage},
    {0xfcc550u, (LPVOID)mts_fade_detour, (LPVOID *)&mts_original_fade},
    {0x1f20c00u, (LPVOID)mts_selection_detour, (LPVOID *)&mts_original_selection},
    {0x1f18170u, (LPVOID)mts_armor_callback_detour, (LPVOID *)&mts_original_armor_callback},
    {0x1f17dc0u, (LPVOID)mts_craft_callback_detour, (LPVOID *)&mts_original_craft_callback},
    {0x1ddd6a0u, (LPVOID)mts_box_callback_detour, (LPVOID *)&mts_original_box_callback},
    {0x1f1a170u, (LPVOID)mts_palico_callback_detour, (LPVOID *)&mts_original_palico_callback},
    {0x1dc4c80u, (LPVOID)mts_pigment_entry_detour, (LPVOID *)&mts_original_pigment_entry},
    {0x1dc4d10u, (LPVOID)mts_pigment_exit_detour, (LPVOID *)&mts_original_pigment_exit},
    {0x1f276e0u, (LPVOID)mts_box_dispatch_detour, (LPVOID *)&mts_original_box_dispatch},
    {0x1f1a180u, (LPVOID)mts_box_change_return_detour, (LPVOID *)&mts_original_box_change_return},
    {0x1f18e40u, (LPVOID)mts_box_other_return_detour, (LPVOID *)&mts_original_box_other_return},
    {0x1f182b0u, (LPVOID)mts_box_pigment_open_detour, (LPVOID *)&mts_original_box_pigment_open},
    {0x1f1a510u, (LPVOID)mts_box_layer_open_detour, (LPVOID *)&mts_original_box_layer_open},
};

static DWORD WINAPI mts_install_worker(void *unused)
{
    (void)unused;
    uintptr_t base = (uintptr_t)GetModuleHandleW(0);
    if (!mts_supported_image(base)) {
        return 0;
    }

    mts_game_base = base;
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        return 0;
    }

    const size_t hook_count = sizeof(mts_hooks) / sizeof(mts_hooks[0]);
    size_t created = 0;
    for (; created < hook_count; ++created) {
        const MtsHookSpec *hook = &mts_hooks[created];
        status = MH_CreateHook((LPVOID)(base + hook->target_rva), hook->detour, hook->original);
        if (status != MH_OK) break;
    }
    if (created != hook_count) {
        while (created) MH_RemoveHook((LPVOID)(base + mts_hooks[--created].target_rva));
        MH_Uninitialize();
        return 0;
    }
    for (size_t i = 0; i < hook_count && status == MH_OK; ++i)
        status = MH_QueueEnableHook((LPVOID)(base + mts_hooks[i].target_rva));
    if (status == MH_OK) status = MH_ApplyQueued();
    if (status != MH_OK) {
        int disabled = 1;
        for (size_t i = 0; i < hook_count; ++i) {
            MH_STATUS st = MH_DisableHook((LPVOID)(base + mts_hooks[i].target_rva));
            if (st != MH_OK && st != MH_ERROR_DISABLED) disabled = 0;
        }
        if (disabled) {
            for (size_t i = 0; i < hook_count; ++i)
                MH_RemoveHook((LPVOID)(base + mts_hooks[i].target_rva));
            MH_Uninitialize();
        }
        return 0;
    }

    InterlockedExchange(&mts_installed, 1);
    quest_install(base);
    mts_resource_install(base);
    hunter_note_install(base);
    hunter_camera_install(base);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE worker = CreateThread(0, 0, mts_install_worker, 0, 0, 0);
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
#endif




