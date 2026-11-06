#include "mts_internal.h"
#include "mts_build_15539686.h"
#include "quest_stub.h"
#include "vendor/include/MinHook.h"
#include <string.h>

static __declspec(thread) unsigned quest_depth;
static volatile LONG quest_active;
static QuestStub *quest_stub;

enum {
    QUEST_ACTION_VTABLE_RVA = 0x32444a8u,
    QUEST_TOP_VTABLE_RVA = 0x3226f78u,
    QUEST_TOP_ANCHOR_RVA = 0x51c46b8u,
    QUEST_TOP_MANAGER_RVA = 0x51c4640u,
    QUEST_ACTION_OWNER_OFFSET = 0x28,
    QUEST_ANCHOR_TOP_OFFSET = 0x561b8,
    QUEST_MANAGER_KEY_OFFSET = 0xae40,
    QUEST_RECORD_A_OFFSET = 8,
    QUEST_RECORD_B_OFFSET = 12,
    QUEST_RECORD_OBJECT_OFFSET = 16,
    QUEST_OBJECT_OWNER_OFFSET = 0x7690
};

typedef struct {
    uintptr_t source, primary, primary_vmt, top, parent, manager, key, record;
    uintptr_t record_key, object, owner, action_owner;
    uint32_t state, mode, context, post, id;
    int32_t record_a, record_b;
    unsigned char action_state, action_phase, parent_flags, object_flags, owner_flags;
} QuestSnapshot;

typedef struct {
    uintptr_t anchor, manager, slot, top;
} QuestTopSource;

static int quest_resolve_top(QuestSnapshot *snapshot, QuestTopSource *source)
{
    enum {
        SLOT_TABLE_OFFSET = 0x3b8,
        SLOT_STRIDE = 0x80,
        SLOT_TOP_OFFSET = 0x58,
        SLOT_COUNT = 32
    };
    uintptr_t base = mts_game_base;
    uintptr_t anchor_address = base + QUEST_TOP_ANCHOR_RVA;
    if (!mts_readable((void *)anchor_address, 8)) return 0;
    source->anchor = *(uintptr_t *)anchor_address;
    if (source->anchor) {
        uintptr_t primary_address = source->anchor + QUEST_ANCHOR_TOP_OFFSET;
        if (!mts_readable((void *)primary_address, 8)) return 0;
        snapshot->primary = *(uintptr_t *)primary_address;
        if (snapshot->primary) {
            if (!mts_readable((void *)snapshot->primary, 8)) return 0;
            snapshot->primary_vmt = *(uintptr_t *)snapshot->primary;
            if (snapshot->primary_vmt == base + QUEST_TOP_VTABLE_RVA) {
                source->top = snapshot->primary;
                snapshot->source = source->anchor;
                return 1;
            }
        }
    }

    uintptr_t manager_address = base + QUEST_TOP_MANAGER_RVA;
    if (!mts_readable((void *)manager_address, 8)) return 0;
    source->manager = *(uintptr_t *)manager_address;
    if (!source->manager) return 0;

    unsigned count = 0;
    for (unsigned i = 1; i <= SLOT_COUNT; i++) {
        uintptr_t slot = source->manager + SLOT_TABLE_OFFSET + i * SLOT_STRIDE + SLOT_TOP_OFFSET;
        if (!mts_readable((void *)slot, 8)) return 0;
        uintptr_t candidate_top = *(uintptr_t *)slot;
        if (candidate_top) {
            if (!mts_readable((void *)candidate_top, 8)) return 0;
            if (*(uintptr_t *)candidate_top == base + QUEST_TOP_VTABLE_RVA) {
                source->top = candidate_top;
                source->slot = slot;
                count++;
            }
        }
    }
    if (count != 1) return 0;
    snapshot->source = source->slot;
    return 1;
}

static int quest_capture_route(QuestSnapshot *snapshot, const QuestTopSource *source)
{
    enum {
        TOP_STATE_OFFSET = 0x1d04,
        TOP_MODE_OFFSET = 0x290c,
        TOP_CONTEXT_OFFSET = 0x2910,
        TOP_POST_OFFSET = 0x2914,
        TOP_PARENT_OFFSET = 0x2100,
        PARENT_FLAGS_OFFSET = 0xc,
        PARENT_ID_OFFSET = 0x12604
    };
    uintptr_t base = mts_game_base;
    uintptr_t top = source->top;
    if (!mts_readable((void *)top, 0x2918)) return 0;
    if (*(uintptr_t *)top != base + QUEST_TOP_VTABLE_RVA) return 0;

    snapshot->top = top;
    snapshot->state = *(uint32_t *)(top + TOP_STATE_OFFSET);
    snapshot->mode = *(uint32_t *)(top + TOP_MODE_OFFSET);
    snapshot->context = *(uint32_t *)(top + TOP_CONTEXT_OFFSET);
    snapshot->post = *(uint32_t *)(top + TOP_POST_OFFSET);
    snapshot->parent = *(uintptr_t *)(top + TOP_PARENT_OFFSET);
    if (snapshot->state != 2 || snapshot->post > 1 || snapshot->context > 1 ||
        (snapshot->post == 1 && snapshot->context != 1)) return 0;

    uintptr_t parent = snapshot->parent;
    if (!mts_readable((void *)parent, 16)) return 0;
    snapshot->parent_flags = *(unsigned char *)(parent + PARENT_FLAGS_OFFSET);
    if (!(snapshot->parent_flags & 0xe)) return 0;
    uintptr_t vtable = *(uintptr_t *)parent;
    if (snapshot->mode == 3) {
        if (vtable != base + 0x34543c8u) return 0;
    } else {
        if (vtable != base + 0x34a0d70u || snapshot->mode > 2) return 0;
        if (!mts_readable((void *)(parent + PARENT_ID_OFFSET), 4)) return 0;
        snapshot->id = *(uint32_t *)(parent + PARENT_ID_OFFSET);
        if (!((snapshot->mode == 0 && snapshot->id == 2) ||
              (snapshot->mode == 1 && (snapshot->id == 0x74 || snapshot->id == 0x9b)) ||
              (snapshot->mode == 2 && (snapshot->id == 0x75 || snapshot->id == 0x9a)))) return 0;
    }

    if (source->slot) {
        if (*(uintptr_t *)(base + QUEST_TOP_MANAGER_RVA) != source->manager ||
            *(uintptr_t *)source->slot != top ||
            *(uintptr_t *)(base + QUEST_TOP_ANCHOR_RVA) != source->anchor) return 0;
    } else if (*(uintptr_t *)(base + QUEST_TOP_ANCHOR_RVA) != source->anchor ||
               *(uintptr_t *)(source->anchor + QUEST_ANCHOR_TOP_OFFSET) != top) return 0;
    return 1;
}

static int quest_capture_owner(QuestSnapshot *snapshot)
{
    enum {
        RECORD_TABLE_OFFSET = 0x48,
        RECORD_STRIDE = 0x740,
        RECORD_COUNT = 24,
        OBJECT_FLAGS_OFFSET = 0xc,
        OWNER_FLAGS_OFFSET = 0xc
    };
    uintptr_t base = mts_game_base;
    uintptr_t manager_address = base + MTS_LOCAL_PLAYER_MANAGER_RVA;
    if (!mts_readable((void *)manager_address, 8)) return 0;
    snapshot->manager = *(uintptr_t *)manager_address;
    uintptr_t key_address = snapshot->manager + QUEST_MANAGER_KEY_OFFSET;
    if (!mts_readable((void *)key_address, 8)) return 0;
    snapshot->key = *(uintptr_t *)key_address;

    unsigned count = 0;
    for (unsigned i = 0; i < RECORD_COUNT; i++) {
        uintptr_t record = snapshot->manager + RECORD_TABLE_OFFSET + i * RECORD_STRIDE;
        if (!mts_readable((void *)record, 24)) return 0;
        uintptr_t record_key = *(uintptr_t *)record;
        int32_t record_a = *(int32_t *)(record + QUEST_RECORD_A_OFFSET);
        int32_t record_b = *(int32_t *)(record + QUEST_RECORD_B_OFFSET);
        if (record_key == snapshot->key && (record_a >= 0 || record_b >= 0)) {
            snapshot->record = record;
            snapshot->record_key = record_key;
            snapshot->record_a = record_a;
            snapshot->record_b = record_b;
            snapshot->object = *(uintptr_t *)(record + QUEST_RECORD_OBJECT_OFFSET);
            count++;
        }
    }
    if (count != 1) return 0;

    if (!mts_readable((void *)(snapshot->object + OBJECT_FLAGS_OFFSET), 1)) return 0;
    if (!mts_readable((void *)(snapshot->object + QUEST_OBJECT_OWNER_OFFSET), 8)) return 0;
    snapshot->object_flags = *(unsigned char *)(snapshot->object + OBJECT_FLAGS_OFFSET);
    snapshot->owner = *(uintptr_t *)(snapshot->object + QUEST_OBJECT_OWNER_OFFSET);
    if (!(snapshot->object_flags & 0xe) || snapshot->owner != snapshot->action_owner) return 0;
    if (!mts_readable((void *)snapshot->owner, 16)) return 0;
    snapshot->owner_flags = *(unsigned char *)(snapshot->owner + OWNER_FLAGS_OFFSET);
    if (*(uintptr_t *)snapshot->owner != base + MTS_CAMERA_VTABLE_RVA ||
        !(snapshot->owner_flags & 0xe)) return 0;
    return 1;
}

static int quest_snapshot(uintptr_t action, QuestSnapshot *snapshot)
{
    enum { ACTION_STATE_OFFSET = 0x60, ACTION_PHASE_OFFSET = 0x90 };
    uintptr_t base = mts_game_base;
    if (!mts_readable((void *)action, 0x91)) return 0;
    if (*(uintptr_t *)action != base + QUEST_ACTION_VTABLE_RVA) return 0;
    snapshot->action_state = *(unsigned char *)(action + ACTION_STATE_OFFSET);
    snapshot->action_phase = *(unsigned char *)(action + ACTION_PHASE_OFFSET);
    snapshot->action_owner = *(uintptr_t *)(action + QUEST_ACTION_OWNER_OFFSET);
    if (snapshot->action_state != 2 || snapshot->action_phase > 1) return 0;

    QuestTopSource source = {0};
    if (!quest_resolve_top(snapshot, &source)) return 0;
    if (!quest_capture_route(snapshot, &source)) return 0;
    if (!quest_capture_owner(snapshot)) return 0;

    /* Recheck the primary source even when the selected top came from a fallback slot. */
    uintptr_t anchor_address = base + QUEST_TOP_ANCHOR_RVA;
    if ((source.anchor && (*(uintptr_t *)anchor_address != source.anchor ||
                           *(uintptr_t *)(source.anchor + QUEST_ANCHOR_TOP_OFFSET) != snapshot->primary ||
                           (snapshot->primary && *(uintptr_t *)snapshot->primary != snapshot->primary_vmt))) ||
        (!source.anchor && *(uintptr_t *)anchor_address != 0)) return 0;

    if (*(uintptr_t *)(base + MTS_LOCAL_PLAYER_MANAGER_RVA) != snapshot->manager ||
        *(uintptr_t *)(snapshot->manager + QUEST_MANAGER_KEY_OFFSET) != snapshot->key ||
        *(uintptr_t *)snapshot->record != snapshot->record_key ||
        *(int32_t *)(snapshot->record + QUEST_RECORD_A_OFFSET) != snapshot->record_a ||
        *(int32_t *)(snapshot->record + QUEST_RECORD_B_OFFSET) != snapshot->record_b ||
        *(uintptr_t *)(snapshot->record + QUEST_RECORD_OBJECT_OFFSET) != snapshot->object ||
        *(uintptr_t *)(snapshot->object + QUEST_OBJECT_OWNER_OFFSET) != snapshot->owner ||
        *(uintptr_t *)(action + QUEST_ACTION_OWNER_OFFSET) != snapshot->owner) return 0;
    return 1;
}

static int quest_snapshots_equal(const QuestSnapshot *left, const QuestSnapshot *right)
{
    return left->source == right->source &&
           left->primary == right->primary &&
           left->primary_vmt == right->primary_vmt &&
           left->top == right->top &&
           left->parent == right->parent &&
           left->manager == right->manager &&
           left->key == right->key &&
           left->record == right->record &&
           left->record_key == right->record_key &&
           left->object == right->object &&
           left->owner == right->owner &&
           left->action_owner == right->action_owner &&
           left->state == right->state &&
           left->mode == right->mode &&
           left->context == right->context &&
           left->post == right->post &&
           left->id == right->id &&
           left->record_a == right->record_a &&
           left->record_b == right->record_b &&
           left->action_state == right->action_state &&
           left->action_phase == right->action_phase &&
           left->parent_flags == right->parent_flags &&
           left->object_flags == right->object_flags &&
           left->owner_flags == right->owner_flags;
}

static float quest_helper(uintptr_t action, float factor)
{
    uint32_t bits;
    memcpy(&bits, &factor, 4);
    if (bits != 0x3f800000u || !InterlockedCompareExchange(&quest_active, 0, 0) ||
        quest_depth) return factor;

    quest_depth++;
    __try {
        __try {
            QuestSnapshot first = {0}, second = {0};
            if (!mts_bypass_held() &&
                quest_snapshot(action, &first) &&
                quest_snapshot(action, &second) &&
                quest_snapshots_equal(&first, &second)) {
                bits = 0x40c00000u;
                memcpy(&factor, &bits, 4);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    } __finally {
        quest_depth--;
    }
    return factor;
}

static int quest_supported(uintptr_t base)
{
    if (!mts_readable((void *)(base + QUEST_ACTION_VTABLE_RVA + 0x30), 0x18) ||
        !mts_readable((void *)(base + QUEST_TOP_VTABLE_RVA + 0x260), 8)) return 0;
    if (*(uintptr_t *)(base + QUEST_ACTION_VTABLE_RVA + 0x30) != base + 0x1273990u ||
        *(uintptr_t *)(base + QUEST_ACTION_VTABLE_RVA + 0x38) != base + 0x1286360u ||
        *(uintptr_t *)(base + QUEST_ACTION_VTABLE_RVA + 0x40) != base + 0x1279ce0u ||
        *(uintptr_t *)(base + QUEST_TOP_VTABLE_RVA + 0x260) != base + 0x115f7f0u) return 0;

    return mts_region_hash(base, 0x1286360, 0x597, UINT64_C(0x3e46f1ab6df97f2d)) &&
           mts_region_hash(base, 0x1286424, 7, UINT64_C(0x214b9cb4dcf15499)) &&
           mts_region_hash(base, 0x1286433, 8, UINT64_C(0x0338e4ae793bf925)) &&
           mts_region_hash(base, MTS_LOCAL_PLAYER_GETTER_RVA, MTS_LOCAL_PLAYER_GETTER_SIZE,
                           MTS_LOCAL_PLAYER_GETTER_HASH) &&
           mts_region_hash(base, 0x3d1fa34, 16, UINT64_C(0x3f37bde04d4c3c6a)) &&
           mts_region_hash(base, 0x3d1fa44, 24, UINT64_C(0x562feaf037669fa2)) &&
           mts_region_hash(base, 0x53d1160, 24, UINT64_C(0xeedf7cbfbff810b2)) &&
           mts_region_hash(base, 0x2e4fe58, 4, UINT64_C(0x4b72477f9c5c2f98)) &&
           mts_region_hash(base, 0x2faf798, 4, UINT64_C(0x4cb8767f9d714215)) &&
           mts_region_hash(base, 0x1279ce0, 0x70, UINT64_C(0x5a37436ec59f0279)) &&
           mts_region_hash(base, 0x1ac43a6, 22, UINT64_C(0xd637a9bb844b776c));
}

void quest_install(uintptr_t base)
{
    void *target = (void *)(base + 0x1286424u), *trampoline = 0;
    HMODULE pinned;
    __try {
        if (!quest_supported(base) ||
            !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                (LPCWSTR)quest_helper, &pinned) ||
            !CreateQuestStub(base, quest_helper, &quest_stub)) return;
        if (MH_CreateHook(target, QuestStubEntry(quest_stub), &trampoline) != MH_OK) {
            DestroyQuestStub(quest_stub);
            quest_stub = 0;
            return;
        }
        static const unsigned char head[] = {0x48, 0x8b, 0x46, 0x28, 0x0f, 0x28, 0xc6};
        unsigned char *p = trampoline;
        if (!mts_readable(p, 21) || memcmp(p, head, 7) || p[7] != 0xff || p[8] != 0x25 ||
            *(uint32_t *)(p + 9) != 0 || *(uintptr_t *)(p + 13) != base + 0x128642b) {
            MH_RemoveHook(target);
            DestroyQuestStub(quest_stub);
            quest_stub = 0;
            return;
        }
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            DestroyQuestStub(quest_stub);
            quest_stub = 0;
            return;
        }
        InterlockedExchange(&quest_active, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* Independent feature remains disabled on faults. */
    }
}
