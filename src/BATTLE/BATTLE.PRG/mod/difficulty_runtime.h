/* Native BATTLE unit only. Card saves retain vanilla-scale room records;
 * exact live values are kept alongside those records until the scene ends. */
#include "difficulty_math.h"
int vs_diff_base_percent = 0, vs_diff_clear_percent = 0;
int vs_diff_active_base = 0, vs_diff_active_clear = 0, vs_diff_active_completions = 0;

typedef struct {
    vs_battle_actor2* actor;
    int main[5];
    int limb[6];
    int current[8], live_current[8];
} vs_diff_baseline;
static vs_diff_baseline vs_diff_original[16] = {0};
static struct {
    const void* record;
    short live[13]; /* vanilla cache shorts at offsets 4..28 */
    short maximum[6];
    vs_diff_baseline original;
} vs_diff_cache[32] = {0};

void vs_diff_begin_scene(int completions)
{
    int i;
    for (i = 0; i < 32; ++i) vs_diff_cache[i].record = 0;
    for (i = 0; i < 16; ++i) vs_diff_original[i].actor = 0;
    vs_diff_active_base = vs_diff_percent(vs_diff_base_percent);
    vs_diff_active_clear = vs_diff_percent(vs_diff_clear_percent);
    vs_diff_active_completions = completions > 99 ? 99 : completions;
}

static int vs_diff_extra(void)
{
    return vs_diff_active_base + vs_diff_active_clear * vs_diff_active_completions;
}

static void vs_diff_init_actor(int slot, int model, vs_battle_actor2* actor)
{
    int i, extra = vs_diff_extra();
    vs_diff_baseline* original;
    if (slot < 0 || slot >= 16) return;
    original = &vs_diff_original[slot];
    original->actor = 0;
    /* Model 8 covers original Merlose and the dynamic companion slot. */
    if (slot == 0 || model == 8 || !extra) return;
    original->actor = actor;
    original->main[0] = actor->maxHP;
    original->main[1] = actor->maxMP;
    original->main[2] = actor->strength;
    original->main[3] = actor->intelligence;
    original->main[4] = actor->agility;
    for (i = 0; i < 6; ++i) original->limb[i] = actor->limbs[i].maxHp;
    original->current[0] = actor->currentHP;
    original->current[1] = actor->currentMP;
    for (i = 0; i < 6; ++i) original->current[i + 2] = actor->limbs[i].hp;
    actor->currentHP = actor->maxHP = vs_diff_scale(actor->maxHP, extra, 999);
    actor->currentMP = actor->maxMP = vs_diff_scale(actor->maxMP, extra, 999);
    actor->strength = actor->totalStrength = vs_diff_scale(actor->strength, extra, 999);
    actor->intelligence = actor->totalIntelligence = vs_diff_scale(actor->intelligence, extra, 999);
    actor->agility = actor->totalAgility = vs_diff_scale(actor->agility, extra, 999);
    for (i = 0; i < 6; ++i)
        actor->limbs[i].hp = actor->limbs[i].maxHp =
            vs_diff_limb(original->limb[i], original->main[0], actor->maxHP);
    original->live_current[0] = actor->currentHP;
    original->live_current[1] = actor->currentMP;
    for (i = 0; i < 6; ++i) original->live_current[i + 2] = actor->limbs[i].hp;
}

static int vs_diff_saved_current(vs_diff_baseline* original, int index,
    int live, int maximum, int original_maximum, int cap)
{
    /* Reuse the exact card value if no damage/healing happened since restore.
     * Otherwise integer conversions would slowly drain HP on repeated saves. */
    if (live != original->live_current[index])
        original->current[index] = vs_diff_ratio(live, original_maximum, maximum, cap);
    original->live_current[index] = live;
    return original->current[index];
}

static void vs_diff_cache_store(void* record, const vs_battle_actor2* actor)
{
    int i, j, slot;
    short* saved = (short*)record;
    vs_diff_baseline* original;
    for (slot = 0; slot < 16; ++slot)
        if (vs_diff_original[slot].actor == actor) break;
    if (slot == 16) return; /* off, Ashley or Merlose: vanilla record */
    original = &vs_diff_original[slot];
    for (i = 0; i < 32; ++i) {
        if (!vs_diff_cache[i].record || vs_diff_cache[i].record == record) {
            vs_diff_cache[i].record = record;
            for (j = 0; j < 13; ++j) vs_diff_cache[i].live[j] = saved[j + 2];
            for (j = 0; j < 6; ++j) vs_diff_cache[i].maximum[j] = actor->limbs[j].maxHp;
            break;
        }
    }
    /* Only vanilla-scale values reach memory cards. Caps are lossy, so retain
     * the original baseline rather than dividing by the requested percentage. */
    saved[2] = vs_diff_saved_current(original, 0, actor->currentHP, actor->maxHP, original->main[0], 999);
    saved[3] = original->main[0];
    saved[4] = vs_diff_saved_current(original, 1, actor->currentMP, actor->maxMP, original->main[1], 999);
    saved[5] = original->main[1];
    saved[6] = vs_diff_ratio(actor->totalStrength, original->main[2], vs_diff_scale(original->main[2], vs_diff_extra(), 999), 999);
    saved[7] = vs_diff_ratio(actor->totalIntelligence, original->main[3], vs_diff_scale(original->main[3], vs_diff_extra(), 999), 999);
    saved[8] = vs_diff_ratio(actor->totalAgility, original->main[4], vs_diff_scale(original->main[4], vs_diff_extra(), 999), 999);
    for (j = 0; j < 6; ++j)
        saved[9 + j] = vs_diff_saved_current(original, j + 2, actor->limbs[j].hp, actor->limbs[j].maxHp, original->limb[j], 32767);
    if (i < 32) vs_diff_cache[i].original = *original;
}

static void vs_diff_cache_restore(const void* record, int slot, int model, vs_battle_actor2* actor)
{
    int i, j, hp, mp, limb[6];
    vs_diff_baseline original;
    if (slot <= 0 || slot >= 16 || model == 8 || !vs_diff_extra()) return;
    for (i = 0; i < 32; ++i) {
        if (vs_diff_cache[i].record == record) {
            const short* live = vs_diff_cache[i].live;
            actor->currentHP = live[0]; actor->maxHP = live[1];
            actor->currentMP = live[2]; actor->maxMP = live[3];
            actor->strength = actor->totalStrength = live[4];
            actor->intelligence = actor->totalIntelligence = live[5];
            actor->agility = actor->totalAgility = live[6];
            for (j = 0; j < 6; ++j) {
                actor->limbs[j].hp = live[7 + j];
                actor->limbs[j].maxHp = vs_diff_cache[i].maximum[j];
            }
            vs_diff_original[slot] = vs_diff_cache[i].original;
            vs_diff_original[slot].actor = actor;
            return;
        }
    }
    /* A card load has vanilla records but no sidecar. Rebuild from those
     * originals exactly once; fresh template limb maxima were retained above. */
    original = vs_diff_original[slot];
    hp = actor->currentHP; mp = actor->currentMP;
    for (j = 0; j < 6; ++j) {
        limb[j] = actor->limbs[j].hp;
        actor->limbs[j].maxHp = original.limb[j];
    }
    i = actor->maxHP; j = actor->maxMP;
    vs_diff_init_actor(slot, model, actor);
    actor->currentHP = vs_diff_ratio(hp, actor->maxHP, i, 999);
    actor->currentMP = vs_diff_ratio(mp, actor->maxMP, j, 999);
    for (j = 0; j < 6; ++j)
        actor->limbs[j].hp = vs_diff_ratio(limb[j], actor->limbs[j].maxHp, original.limb[j], 32767);
    vs_diff_original[slot].current[0] = hp;
    vs_diff_original[slot].current[1] = mp;
    vs_diff_original[slot].live_current[0] = actor->currentHP;
    vs_diff_original[slot].live_current[1] = actor->currentMP;
    for (j = 0; j < 6; ++j) {
        vs_diff_original[slot].current[j + 2] = limb[j];
        vs_diff_original[slot].live_current[j + 2] = actor->limbs[j].hp;
    }
}
