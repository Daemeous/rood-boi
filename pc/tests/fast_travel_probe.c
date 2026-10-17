/* Fast travel's rules (pc/src/fast_travel_rules.c) without the game: MENU0's
 * tables against the decompiled 84.c, the packing against 15AC.c's own
 * arithmetic, the cost modes, whose unlock is in effect, the port's refusals,
 * the Travel button's guard and the RPC report. */
#include "fast_travel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                            \
    do {                                            \
        if (!(cond)) {                              \
            fprintf(stderr, "FAIL %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);           \
            fputc('\n', stderr);                    \
            ++failures;                             \
        }                                           \
    } while (0)

/* MENU0 84.c's `name[] = { ... }` initialiser, read from the source tree. */
static int read_table(const char* name, int* out, int max)
{
    char path[1024], key[96];
    static char text[1 << 16];
    snprintf(path, sizeof path, "%s/src/MENU/MENU0.PRG/84.c", ROOD_ROOT);
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    snprintf(key, sizeof key, "%s[] = {", name);
    char* at = strstr(text, key);
    if (!at)
        return -1;
    at += strlen(key);
    int count = 0;
    while (count < max) {
        char* end;
        long v = strtol(at, &end, 0);
        if (end == at)
            break;
        out[count++] = (int)v;
        at = end;
        while (*at == ',' || *at == ' ' || *at == '\n' || *at == '\r')
            ++at;
        if (*at == '}')
            break;
    }
    return count;
}

/* 15AC.c returnIfReady, line for line (rowCount's stale high bits are masked
 * away), then vs_menu0_exec teleportMenu's `executeAbility.s32 = i - 1`. */
static int menu0_packed(int point, int cost, int rowCount)
{
    int i = pc_ft_save_point_data[point] + 1;
    int v1 = i & 0x1FF;
    int init = ~0x3E00;
    int v0 = (rowCount & ~0x1FF) | v1;
    v1 = (i >> 7) & 0x3E00;
    v0 &= init;
    v0 |= v1;
    int a0 = (i >> 0x18) & 0x1F;
    a0 <<= 0xE;
    v1 = cost << 0x13;
    v0 &= 0x3FFF;
    return (v0 | a0 | v1) - 1;
}

static pc_ft_settings on(int cost)
{
    pc_ft_settings c = { 1, cost, 15, 0.5f };
    return c;
}

/* On Circle 2 (MENU0 index 2, location 0x199), MP 40, prologue over,
 * Circles 0-2 found, nothing in the way. */
static pc_ft_state circle2(void)
{
    pc_ft_state s;
    memset(&s, 0, sizeof s);
    s.intro = 3;
    s.circle = 3;
    s.here = 2;
    s.mask = 8; /* sheathed only */
    s.mp = 40;
    s.states[0] = s.states[1] = s.states[2] = 1;
    return s;
}

static void tables(void)
{
    int data[64], costs[64];
    CHECK(read_table("_savePointData", data, 64) == PC_FT_POINTS, "84.c _savePointData has 48 entries");
    CHECK(read_table("savePointCosts", costs, 64) == PC_FT_POINTS, "84.c savePointCosts has 48 entries");
    for (int i = 0; i < PC_FT_POINTS; ++i) {
        CHECK(pc_ft_save_point_data[i] == data[i], "_savePointData[%d] %#x (84.c %#x)", i, pc_ft_save_point_data[i], data[i]);
        CHECK(pc_ft_save_point_costs[i] == costs[i], "savePointCosts[%d] %d (84.c %d)", i, pc_ft_save_point_costs[i], costs[i]);
    }
    for (int i = 0; i < PC_FT_USED; ++i)
        CHECK(pc_ft_save_point_data[i] != 0, "entry %d is used", i);
    CHECK(pc_ft_save_point_data[PC_FT_USED] == 0, "35 entries used");
}

static void packing(void)
{
    static const int costs[] = { 0, 1, 15, 19, 87, 4095 };
    for (int p = 0; p < PC_FT_USED; ++p)
        for (unsigned k = 0; k < sizeof costs / sizeof costs[0]; ++k)
            CHECK(pc_ft_packed(p, costs[k]) == menu0_packed(p, costs[k], 0x5A5A00 + p),
                "point %d cost %d: %#x (MENU0 %#x)", p, costs[k], pc_ft_packed(p, costs[k]),
                menu0_packed(p, costs[k], 0x5A5A00 + p));
    CHECK(pc_ft_here(0x1B) == 0 && pc_ft_here(0x199) == 2 && pc_ft_here(0x3FF) == PC_FT_POINTS
            && pc_ft_here(0) == PC_FT_USED,
        "this room's point: MENU0's search (48 none; location 0 matches the first unused entry, as in 84.c)");
}

static void costs(void)
{
    pc_ft_state s = circle2();
    CHECK(pc_ft_raw_cost(2, 2) == 15 && pc_ft_raw_cost(2, 0) == 23 && pc_ft_raw_cost(2, 18) == 79,
        "_getTeleportCost: 15 here, + 4 per index between");
    CHECK(pc_ft_raw_cost(PC_FT_POINTS, 3) == 15 + 4 * 44, "no point in this room: 48");
    pc_ft_settings c = on(PC_FT_COST_FREE);
    CHECK(pc_ft_cost(&c, &s, 23) == 0, "free");
    c = on(PC_FT_COST_FLAT);
    CHECK(pc_ft_cost(&c, &s, 23) == 15, "flat 15");
    c.flat = 9000;
    CHECK(pc_ft_cost(&c, &s, 23) == PC_FT_COST_MAX, "flat: the packed field's 12 bits");
    c = on(PC_FT_COST_SCALED);
    CHECK(pc_ft_cost(&c, &s, 23) == 12, "scaled x0.5, rounded");
    c.scale = 1e30f;
    CHECK(pc_ft_cost(&c, &s, 23) == PC_FT_COST_MAX, "scaled: a huge scale caps at the 12 bits (not 0)");
    c.scale = INFINITY;
    CHECK(pc_ft_cost(&c, &s, 23) == PC_FT_COST_MAX, "scaled: an infinite scale caps");
    c.scale = -INFINITY;
    CHECK(pc_ft_cost(&c, &s, 23) == 0, "scaled: a negative scale is free");
    c.scale = NAN;
    CHECK(pc_ft_cost(&c, &s, 23) == 23, "scaled: NaN is the game's");
    c = on(PC_FT_COST_FLAT);
    c.flat = -5;
    CHECK(pc_ft_cost(&c, &s, 23) == 0, "flat: a negative MP is free");
    c = on(PC_FT_COST_GAME);
    CHECK(pc_ft_cost(&c, &s, 23) == 23, "game");
    c = on(PC_FT_COST_FREE);
    s.game = 1;
    CHECK(pc_ft_cost(&c, &s, 23) == 23, "the game's unlock: its own cost whatever the mode");
}

static void unlock(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(pc_ft_whose(&c, &s) == PC_FT_PORT && pc_ft_unlock(&c, &s) == 1, "the port's unlock");
    s.intro = 2;
    CHECK(pc_ft_whose(&c, &s) == PC_FT_OFF && pc_ft_unlock(&c, &s) == 0, "not during the prologue");
    s = circle2();
    c.enabled = 0;
    CHECK(pc_ft_unlock(&c, &s) == 0, "qol_fast_travel 0");
    s.game = 1;
    CHECK(pc_ft_whose(&c, &s) == PC_FT_GAME && pc_ft_unlock(&c, &s) == 1, "the game's unlock wins (setting off)");
    s.game = 2;
    CHECK(pc_ft_unlock(&c, &s) == 2, "the spirit paling: 2, as MENU0 reads it");
    c.enabled = 1;
    CHECK(pc_ft_unlock(&c, &s) == 2, "the paling wins over the port's unlock");
    CHECK(pc_ft_counts_as_magic(&s) == 1, "the game's unlock counts as magic");
    s = circle2();
    CHECK(pc_ft_counts_as_magic(&s) == 0, "the port's does not");
}

static void masks(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == (PC_FT_MAGIC_MASK & ~PC_FT_NO_MP), "free on a Circle: MP 0 greys nothing");
    s.circle = 0;
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "off a Circle: the game's mask");
    s = circle2();
    c = on(PC_FT_COST_FLAT);
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "a cost: MP 0 greys");
    c = on(PC_FT_COST_FREE);
    s.game = 1;
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "the game's unlock: the game's mask");
    s = circle2();
    CHECK(pc_ft_menu_bits(&c, &s) == 1, "the Magic command on a Circle");
    s.circle = 0;
    CHECK(pc_ft_menu_bits(&c, &s) == 0, "not off a Circle");
    s = circle2();
    s.game = 1;
    CHECK(pc_ft_menu_bits(&c, &s) == 0, "the game's unlock sets menuFlags itself");
}

static void refusals(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(!pc_ft_blocked(&c, &s), "nothing in the way");
    s.warped = 1;
    CHECK(pc_ft_blocked(&c, &s) && strstr(pc_ft_blocked(&c, &s), "Room Browser"), "R7: warped");
    s = circle2();
    s.no_save = 1;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R3: unkA7");
    s = circle2();
    s.states[2] = 0;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R1: this room's Circle not recorded (MENU0's nearestSavePoint)");
    s = circle2();
    s.here = PC_FT_POINTS;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R1: no Circle in MENU0's table for this room");
    s = circle2();
    s.no_save = 1, s.game = 1;
    CHECK(!pc_ft_blocked(&c, &s), "the game's unlock: the game's rules only");
    s = circle2();
    CHECK(!pc_ft_refused(&c, &s, 0) && !pc_ft_refused(&c, &s, 1), "found Circles");
    CHECK(pc_ft_refused(&c, &s, 3) != NULL, "R4: not found");
    s.states[1] = 2;
    CHECK(pc_ft_refused(&c, &s, 1) && strstr(pc_ft_refused(&c, &s, 1), "paling"), "R4: the spirit paling");
    s = circle2();
    c = on(PC_FT_COST_GAME);
    s.mp = 18;
    CHECK(pc_ft_refused(&c, &s, 1) != NULL && !pc_ft_refused(&c, &s, 2), "MP short (19) for Circle 1; here costs 15");
    s = circle2();
    s.mask |= 2;
    CHECK(pc_ft_refused(&c, &s, 1) && strstr(pc_ft_refused(&c, &s, 1), "carrying"), "the mask: carrying");
    s = circle2();
    s.mask |= PC_FT_NO_MP, s.mp = 0;
    c = on(PC_FT_COST_FREE);
    CHECK(!pc_ft_refused(&c, &s, 1), "free: MP 0 refuses nothing");
}

static void shortcut(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(!pc_ft_shortcut(&c, &s), "the Travel button opens the list");
    s.circle = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "off a Circle");
    s = circle2();
    s.event = 1;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "an event");
    s = circle2();
    s.busy = 1;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "Ashley busy (or L2 / R2 held)");
    s = circle2();
    s.states[0] = s.states[1] = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "no other Circle found");
    s = circle2();
    s.game = 2;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "the spirit paling");
    s = circle2();
    c.enabled = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "travel off, the game's spell not learned");
    s.game = 1, s.mp = 99;
    CHECK(!pc_ft_shortcut(&c, &s), "the game's Teleportation: the button opens its list too");
    s.no_save = 1;
    CHECK(!pc_ft_shortcut(&c, &s), "... with the game's rules only");
}

static void report(void)
{
    static pc_ft_report r;
    static char out[1 << 16];
    memset(&r, 0, sizeof r);
    r.settings = on(PC_FT_COST_FREE);
    r.state = circle2();
    int n = pc_ft_json(&r, out, sizeof out);
    CHECK(n > 0 && n < (int)sizeof out, "the JSON fits");
    CHECK(strstr(out, "\"whose\":\"port\"") && strstr(out, "\"cost\":\"free\"") && strstr(out, "\"why\":\"\""),
        "whose, cost, the Travel button's reason");
    CHECK(strstr(out, "{\"i\":2,\"state\":1,\"raw\":15,\"cost\":0,\"current\":true"), "the current Circle's row: %.200s", out);
    CHECK(pc_ft_json(&r, out, 64) < 0, "too small: refused, not cut");
}

int main(void)
{
    tables();
    packing();
    costs();
    unlock();
    masks();
    refusals();
    shortcut();
    report();
    if (failures)
        return 1;
    puts("PASS: fast travel rules (tables, packing, cost modes, unlock, mask, refusals, Travel button, JSON)");
    return 0;
}
