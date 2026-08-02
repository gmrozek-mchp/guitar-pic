#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>
#include <stddef.h>          /* NULL */
#include <stdint.h>

#include "model_infer.h"     /* model_def_t, MODEL_SEL_* */
#include "model_weights.h"   /* model_hard (sets MODEL_ARCH_DIMS + MODEL_DEFAULT) */

/*
 * Difficulty registry: maps a selection index (MODEL_SEL_*) to the int8 model
 * that plays it. All models share the architecture dims (model_weights.h
 * _Static_asserts this); only weights/thresholds differ.
 *
 * Only the "hard" model is trained today. To add a trained difficulty, drop its
 * generated header here and flag it — e.g. for easy:
 *     #include "model_weights_easy.h"
 *     #define MODEL_HAVE_EASY
 * and register the .h in the mplab fileSet. Until then the untrained slots alias
 * to model_hard so all four difficulties are selectable (they behave as hard).
 *
 * This is an integration header (not generated) — editing it to register a model
 * is expected; the generated model_weights_*.h are the do-not-edit files.
 */

#define MODEL_HAVE_HARD   /* the one trained model (model_weights.h) */

/* Concrete difficulty -> model (untrained aliases to hard). Indexed by
 * MODEL_SEL_EASY..MODEL_SEL_EXPERT (0..3). */
static const model_def_t *const MODEL_BY_DIFFICULTY[MODEL_SEL_DIFFICULTIES] = {
#ifdef MODEL_HAVE_EASY
    &model_easy,
#else
    &model_hard,
#endif
#ifdef MODEL_HAVE_MEDIUM
    &model_medium,
#else
    &model_hard,
#endif
    &model_hard,   /* MODEL_SEL_HARD */
#ifdef MODEL_HAVE_EXPERT
    &model_expert,
#else
    &model_hard,
#endif
};

/* True where the slot has real trained weights (vs aliased to hard). Indexed by
 * concrete difficulty; the CLI marks placeholders from this. */
static const bool MODEL_DIFFICULTY_TRAINED[MODEL_SEL_DIFFICULTIES] = {
#ifdef MODEL_HAVE_EASY
    true,
#else
    false,
#endif
#ifdef MODEL_HAVE_MEDIUM
    true,
#else
    false,
#endif
    true,          /* hard */
#ifdef MODEL_HAVE_EXPERT
    true,
#else
    false,
#endif
};

/* Human-readable name per selection index (0..MODEL_SEL_COUNT-1). */
static const char *const MODEL_SEL_NAMES[MODEL_SEL_COUNT] = {
    "easy", "medium", "hard", "expert", "auto",
};

/* Resolve a selection index to the model to run. AUTO is a policy slot, not a
 * static model: for now it resolves to hard (adaptive selection is future work —
 * this is the single hook where it will go). Out-of-range falls back to the
 * default. *eff_difficulty (if non-NULL) receives the concrete difficulty in
 * effect (0..3) — meaningful for AUTO, where the selection index isn't itself a
 * difficulty. */
static inline const model_def_t *model_resolve(uint8_t sel, uint8_t *eff_difficulty)
{
    uint8_t diff;
    switch (sel) {
        case MODEL_SEL_EASY:
        case MODEL_SEL_MEDIUM:
        case MODEL_SEL_HARD:
        case MODEL_SEL_EXPERT:
            diff = sel;
            break;
        case MODEL_SEL_AUTO:
            diff = MODEL_SEL_HARD;   /* stub: resolve auto to hard */
            break;
        default:
            diff = MODEL_SEL_DEFAULT;
            break;
    }
    if (eff_difficulty != NULL) { *eff_difficulty = diff; }
    return MODEL_BY_DIFFICULTY[diff];
}

#endif /* MODELS_H */
