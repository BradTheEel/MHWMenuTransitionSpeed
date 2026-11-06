#ifndef MTS_FADE_RULES_H
#define MTS_FADE_RULES_H

#include <stdint.h>

typedef enum MtsFadeContext {
    MTS_FADE_CONTEXT_NONE = 0,
    MTS_FADE_CONTEXT_SELECTION_01 = 1,
    MTS_FADE_CONTEXT_ARMOR_DEFAULT = 2,
    MTS_FADE_CONTEXT_CRAFT_CALLBACK = 3,
    MTS_FADE_CONTEXT_SELECTION_38 = 4,
    MTS_FADE_CONTEXT_SELECTION_120 = 5,
    MTS_FADE_CONTEXT_ARMOR_DE20 = 6,
    MTS_FADE_CONTEXT_ARMOR_DE80 = 7,
    MTS_FADE_CONTEXT_BOX_CALLBACK = 8,
    MTS_FADE_CONTEXT_SELECTION_43 = 9,
    MTS_FADE_CONTEXT_SELECTION_5E = 10,
    MTS_FADE_CONTEXT_SELECTION_5F = 11,
    MTS_FADE_CONTEXT_ARMOR_DD00 = 12,
    MTS_FADE_CONTEXT_ARMOR_E1B0 = 13,
    MTS_FADE_CONTEXT_PALICO_CALLBACK = 14,
    MTS_FADE_CONTEXT_PIGMENT_ENTRY = 15,
    MTS_FADE_CONTEXT_PIGMENT_EXIT = 16,
    MTS_FADE_CONTEXT_BOX_DISPATCH = 17,
    MTS_FADE_CONTEXT_BOX_CHANGE_RETURN = 18,
    MTS_FADE_CONTEXT_BOX_PIGMENT_OPEN = 19,
    MTS_FADE_CONTEXT_BOX_OTHER_RETURN = 20,
    MTS_FADE_CONTEXT_BOX_LAYER_OPEN = 21,
    MTS_FADE_CONTEXT_BOX_OTHER_RETURN_BD80 = 22
} MtsFadeContext;

typedef struct MtsFadeRule MtsFadeRule;

MtsFadeContext mts_fade_context_from_selector(uint32_t selector);
int mts_fade_context_uses_selection(MtsFadeContext context);
const MtsFadeRule *mts_fade_rule_for_caller(uint32_t caller_rva);
int mts_fade_rule_matches_context(const MtsFadeRule *rule, uint32_t context);
int mts_fade_shape_matches(const MtsFadeRule *rule, int context_matches,
                           uint32_t out_bits, uint32_t in_bits, uint32_t hold_bits);
void mts_fade_apply(const MtsFadeRule *rule, float *out, float *in, float *hold);

__declspec(dllexport) int __cdecl MTS_FadeSelect(uint32_t caller, uint32_t context,
                                                uint32_t component_valid, uint32_t shift);

#endif
