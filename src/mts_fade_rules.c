#include "mts_fade_rules.h"

#include <stddef.h>

static const struct {
    uint32_t selector;
    MtsFadeContext context;
} mts_fade_selectors[] = {
    {1u,     MTS_FADE_CONTEXT_SELECTION_01},
    {0x38u,  MTS_FADE_CONTEXT_SELECTION_38},
    {0x120u, MTS_FADE_CONTEXT_SELECTION_120},
    {0x43u,  MTS_FADE_CONTEXT_SELECTION_43},
    {0x5eu,  MTS_FADE_CONTEXT_SELECTION_5E},
    {0x5fu,  MTS_FADE_CONTEXT_SELECTION_5F},
};

typedef struct MtsFadeProfile {
    uint32_t out_bits, in_bits, hold_bits;
    float out, in, hold;
} MtsFadeProfile;

static const MtsFadeProfile mts_fade_full = {
    0x3e19999au, 0x3e19999au, 0x3eb33333u,
    0.05f, 0.05f, 0.10f
};

static const MtsFadeProfile mts_fade_box_entry = {
    0x3e19999au, 0x3e19999au, 0u,
    0.05f, 0.05f, 0.0f
};

static const MtsFadeProfile mts_fade_box_return = {
    0u, 0x3e19999au, 0u,
    0.0f, 0.05f, 0.0f
};

static const MtsFadeProfile mts_fade_pigment_exit = {
    0x3f000000u, 0x3f000000u, 0u,
    0.05f, 0.05f, 0.0f
};

struct MtsFadeRule {
    uint32_t caller_rva;
    uint32_t contexts;
    const MtsFadeProfile *profile;
};

#define MTS_FADE_CONTEXT_BIT(context) (UINT32_C(1) << (context))

static const MtsFadeRule mts_fade_rules[] = {
    {0x1f229cau, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_01) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_120) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_43) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_5E), &mts_fade_full},
    {0x1f181d5u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_ARMOR_DEFAULT) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_ARMOR_DE20) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_ARMOR_DE80) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_ARMOR_DD00) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_ARMOR_E1B0), &mts_fade_full},
    {0x1f17e2au, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_CRAFT_CALLBACK), &mts_fade_full},
    {0x1f20bd1u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_NONE), &mts_fade_full},
    {0x1f21a92u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_38), &mts_fade_full},
    {0x1ddf451u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_NONE), &mts_fade_box_entry},
    {0x1ddd6efu, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_CALLBACK), &mts_fade_box_return},
    {0x1f227d4u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_SELECTION_5F), &mts_fade_full},
    {0x1f14dbbu, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_PALICO_CALLBACK), &mts_fade_full},
    {0x1dc1d80u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_PIGMENT_ENTRY), &mts_fade_full},
    {0x1dc2c2bu, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_PIGMENT_EXIT), &mts_fade_pigment_exit},
    {0x1f27b87u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_DISPATCH), &mts_fade_full},
    {0x1f1a1e5u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_CHANGE_RETURN), &mts_fade_full},
    {0x1f18306u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_PIGMENT_OPEN), &mts_fade_full},
    {0x1f18eaau, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_OTHER_RETURN) |
                 MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_OTHER_RETURN_BD80), &mts_fade_full},
    {0x1f1a568u, MTS_FADE_CONTEXT_BIT(MTS_FADE_CONTEXT_BOX_LAYER_OPEN), &mts_fade_full},
};

MtsFadeContext mts_fade_context_from_selector(uint32_t selector)
{
    for (size_t i = 0; i < sizeof(mts_fade_selectors) / sizeof(mts_fade_selectors[0]); ++i) {
        if (mts_fade_selectors[i].selector == selector)
            return mts_fade_selectors[i].context;
    }
    return MTS_FADE_CONTEXT_NONE;
}

int mts_fade_context_uses_selection(MtsFadeContext context)
{
    for (size_t i = 0; i < sizeof(mts_fade_selectors) / sizeof(mts_fade_selectors[0]); ++i) {
        if (mts_fade_selectors[i].context == context)
            return 1;
    }
    return 0;
}

const MtsFadeRule *mts_fade_rule_for_caller(uint32_t caller_rva)
{
    for (size_t i = 0; i < sizeof(mts_fade_rules) / sizeof(mts_fade_rules[0]); ++i) {
        if (mts_fade_rules[i].caller_rva == caller_rva)
            return &mts_fade_rules[i];
    }
    return NULL;
}

int mts_fade_rule_matches_context(const MtsFadeRule *rule, uint32_t context)
{
    return rule && context < 32u && (rule->contexts & MTS_FADE_CONTEXT_BIT(context)) != 0;
}

int mts_fade_shape_matches(const MtsFadeRule *rule, int context_matches,
                           uint32_t out_bits, uint32_t in_bits, uint32_t hold_bits)
{
    const MtsFadeProfile *profile = rule ? rule->profile : &mts_fade_full;
    /* Full fades can reach the Shift check even without a matching rule. */
    return (profile == &mts_fade_full || context_matches) &&
           out_bits == profile->out_bits && in_bits == profile->in_bits &&
           hold_bits == profile->hold_bits;
}

void mts_fade_apply(const MtsFadeRule *rule, float *out, float *in, float *hold)
{
    *out = rule->profile->out;
    *in = rule->profile->in;
    *hold = rule->profile->hold;
}

__declspec(dllexport) int __cdecl MTS_FadeSelect(uint32_t caller, uint32_t context,
                                                uint32_t component_valid, uint32_t shift)
{
    if (!component_valid || shift) return 0;
    return mts_fade_rule_matches_context(mts_fade_rule_for_caller(caller), context);
}
