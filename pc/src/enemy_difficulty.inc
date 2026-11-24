/* Native BATTLE unit only. Card saves retain vanilla-scale room records;
 * exact live values are kept alongside those records until the scene ends. */
#include "enemy_difficulty_math.h"
extern int pc_difficulty_base_percent, pc_difficulty_clear_percent;
int pc_difficulty_active_base, pc_difficulty_active_clear, pc_difficulty_active_completions;

typedef struct {
    vs_battle_actor2* actor;
    int main[5];
    int limb[6];
    int current[8], live_current[8];
} pc_difficulty_baseline;
static pc_difficulty_baseline pc_difficulty_original[16];
static struct {
    const void* record;
    short live[13]; /* vanilla cache shorts at offsets 4..28 */
    short maximum[6];
    pc_difficulty_baseline original;
} pc_difficulty_cache[32];

void pc_difficulty_begin_scene(int completions)
{
    int i;
    for (i = 0; i < 32; ++i) pc_difficulty_cache[i].record = 0;
    for (i = 0; i < 16; ++i) pc_difficulty_original[i].actor = 0;
    pc_difficulty_active_base = pc_difficulty_percent(pc_difficulty_base_percent);
    pc_difficulty_active_clear = pc_difficulty_percent(pc_difficulty_clear_percent);
    pc_difficulty_active_completions = completions > 99 ? 99 : completions;
}

static int pc_difficulty_extra(void)
{
    return pc_difficulty_active_base + pc_difficulty_active_clear * pc_difficulty_active_completions;
}

static void pc_difficulty_init_actor(int slot, int model, vs_battle_actor2* actor)
{
    int i, extra = pc_difficulty_extra();
    pc_difficulty_baseline* original;
    if (slot < 0 || slot >= 16) return;
    original = &pc_difficulty_original[slot];
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
    actor->currentHP = actor->maxHP = pc_difficulty_scale(actor->maxHP, extra, 999);
    actor->currentMP = actor->maxMP = pc_difficulty_scale(actor->maxMP, extra, 999);
    actor->strength = actor->totalStrength = pc_difficulty_scale(actor->strength, extra, 999);
    actor->intelligence = actor->totalIntelligence = pc_difficulty_scale(actor->intelligence, extra, 999);
    actor->agility = actor->totalAgility = pc_difficulty_scale(actor->agility, extra, 999);
    for (i = 0; i < 6; ++i)
        actor->limbs[i].hp = actor->limbs[i].maxHp =
            pc_difficulty_limb(original->limb[i], original->main[0], actor->maxHP);
    original->live_current[0] = actor->currentHP;
    original->live_current[1] = actor->currentMP;
    for (i = 0; i < 6; ++i) original->live_current[i + 2] = actor->limbs[i].hp;
}

static int pc_difficulty_saved_current(pc_difficulty_baseline* original, int index,
    int live, int maximum, int original_maximum, int cap)
{
    /* Reuse the exact card value if no damage/healing happened since restore.
     * Otherwise integer conversions would slowly drain HP on repeated saves. */
    if (live != original->live_current[index])
        original->current[index] = pc_difficulty_ratio(live, original_maximum, maximum, cap);
    original->live_current[index] = live;
    return original->current[index];
}

static void pc_difficulty_cache_store(void* record, const vs_battle_actor2* actor)
{
    int i, j, slot;
    short* saved = (short*)record;
    pc_difficulty_baseline* original;
    for (slot = 0; slot < 16; ++slot)
        if (pc_difficulty_original[slot].actor == actor) break;
    if (slot == 16) return; /* off, Ashley or Merlose: vanilla record */
    original = &pc_difficulty_original[slot];
    for (i = 0; i < 32; ++i) {
        if (!pc_difficulty_cache[i].record || pc_difficulty_cache[i].record == record) {
            pc_difficulty_cache[i].record = record;
            for (j = 0; j < 13; ++j) pc_difficulty_cache[i].live[j] = saved[j + 2];
            for (j = 0; j < 6; ++j) pc_difficulty_cache[i].maximum[j] = actor->limbs[j].maxHp;
            break;
        }
    }
    /* Only vanilla-scale values reach memory cards. Caps are lossy, so retain
     * the original baseline rather than dividing by the requested percentage. */
    saved[2] = pc_difficulty_saved_current(original, 0, actor->currentHP, actor->maxHP, original->main[0], 999);
    saved[3] = original->main[0];
    saved[4] = pc_difficulty_saved_current(original, 1, actor->currentMP, actor->maxMP, original->main[1], 999);
    saved[5] = original->main[1];
    saved[6] = pc_difficulty_ratio(actor->totalStrength, original->main[2], pc_difficulty_scale(original->main[2], pc_difficulty_extra(), 999), 999);
    saved[7] = pc_difficulty_ratio(actor->totalIntelligence, original->main[3], pc_difficulty_scale(original->main[3], pc_difficulty_extra(), 999), 999);
    saved[8] = pc_difficulty_ratio(actor->totalAgility, original->main[4], pc_difficulty_scale(original->main[4], pc_difficulty_extra(), 999), 999);
    for (j = 0; j < 6; ++j)
        saved[9 + j] = pc_difficulty_saved_current(original, j + 2, actor->limbs[j].hp, actor->limbs[j].maxHp, original->limb[j], 32767);
    if (i < 32) pc_difficulty_cache[i].original = *original;
}

static void pc_difficulty_cache_restore(const void* record, int slot, int model, vs_battle_actor2* actor)
{
    int i, j, hp, mp, limb[6];
    pc_difficulty_baseline original;
    if (slot <= 0 || slot >= 16 || model == 8 || !pc_difficulty_extra()) return;
    for (i = 0; i < 32; ++i) {
        if (pc_difficulty_cache[i].record == record) {
            const short* live = pc_difficulty_cache[i].live;
            actor->currentHP = live[0]; actor->maxHP = live[1];
            actor->currentMP = live[2]; actor->maxMP = live[3];
            actor->strength = actor->totalStrength = live[4];
            actor->intelligence = actor->totalIntelligence = live[5];
            actor->agility = actor->totalAgility = live[6];
            for (j = 0; j < 6; ++j) {
                actor->limbs[j].hp = live[7 + j];
                actor->limbs[j].maxHp = pc_difficulty_cache[i].maximum[j];
            }
            pc_difficulty_original[slot] = pc_difficulty_cache[i].original;
            pc_difficulty_original[slot].actor = actor;
            return;
        }
    }
    /* A card load has vanilla records but no sidecar. Rebuild from those
     * originals exactly once; fresh template limb maxima were retained above. */
    original = pc_difficulty_original[slot];
    hp = actor->currentHP; mp = actor->currentMP;
    for (j = 0; j < 6; ++j) {
        limb[j] = actor->limbs[j].hp;
        actor->limbs[j].maxHp = original.limb[j];
    }
    i = actor->maxHP; j = actor->maxMP;
    pc_difficulty_init_actor(slot, model, actor);
    actor->currentHP = pc_difficulty_ratio(hp, actor->maxHP, i, 999);
    actor->currentMP = pc_difficulty_ratio(mp, actor->maxMP, j, 999);
    for (j = 0; j < 6; ++j)
        actor->limbs[j].hp = pc_difficulty_ratio(limb[j], actor->limbs[j].maxHp, original.limb[j], 32767);
    pc_difficulty_original[slot].current[0] = hp;
    pc_difficulty_original[slot].current[1] = mp;
    pc_difficulty_original[slot].live_current[0] = actor->currentHP;
    pc_difficulty_original[slot].live_current[1] = actor->currentMP;
    for (j = 0; j < 6; ++j) {
        pc_difficulty_original[slot].current[j + 2] = limb[j];
        pc_difficulty_original[slot].live_current[j + 2] = actor->limbs[j].hp;
    }
}
