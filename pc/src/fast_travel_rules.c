/* Fast travel's rules (fast_travel.h): plain C, no game headers. */
#include "fast_travel.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

/* MENU0 84.c, copied (fast_travel_probe compares them with the source). */
const int pc_ft_save_point_data[PC_FT_POINTS] = { 0x0305001B, 0x0801000E, 0x04030199, 0x0C0C001C, 0x070C0028,
    0x040200CD, 0x0F070035, 0x08060093, 0x060000CE, 0x09080106, 0x09080111, 0x0C030098, 0x090800E0, 0x060900C8,
    0x050800CA, 0x050F00CC, 0x0109009B, 0x0A07007C, 0x000400CF, 0x080800EC, 0x020400F1, 0x0705011D, 0x06080120,
    0x04080139, 0x04080147, 0x0904014F, 0x010300A2, 0x150C00A3, 0x030200D0, 0x060600D1, 0x1007008B, 0x010400AB,
    0x05020055, 0x07060063, 0x0A0400D2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
const char pc_ft_save_point_costs[PC_FT_POINTS] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 };
const char* const pc_ft_cost_names[PC_FT_COST_MODES] = { "free", "flat", "scaled", "game" };

static const char PALING[] = "The spirit paling prevents teleportation.";

int pc_ft_here(int location)
{
    int i;
    for (i = 0; i < PC_FT_POINTS; ++i)
        if ((pc_ft_save_point_data[i] & 0xFFFF) == location)
            break;
    return i;
}

int pc_ft_raw_cost(int here, int target)
{
    int i = here, j, cost = PC_FT_HERE;
    if (target < i) {
        i = target;
        target = here;
    }
    for (j = i; j < target && j < PC_FT_POINTS; ++j)
        cost += pc_ft_save_point_costs[j];
    return cost;
}

int pc_ft_whose(const pc_ft_settings* c, const pc_ft_state* s)
{
    if (s->game)
        return PC_FT_GAME;
    return c->enabled && s->intro >= 3 ? PC_FT_PORT : PC_FT_OFF;
}

int pc_ft_unlock(const pc_ft_settings* c, const pc_ft_state* s)
{
    return s->game ? s->game : pc_ft_whose(c, s) == PC_FT_PORT;
}

int pc_ft_cost(const pc_ft_settings* c, const pc_ft_state* s, int raw)
{
    float f;
    if (pc_ft_whose(c, s) != PC_FT_PORT)
        return raw;
    switch (c->cost) {
    case PC_FT_COST_FREE:
        return 0;
    case PC_FT_COST_FLAT:
        return c->flat < 0 ? 0 : c->flat > PC_FT_COST_MAX ? PC_FT_COST_MAX : c->flat;
    case PC_FT_COST_SCALED:
        if (c->scale != c->scale)
            return raw; /* (NaN: the game's) */
        /* clamped before rounding: lroundf of a huge or infinite value (a
         * hand-edited settings file) is unspecified, and long is 32 bits on Windows */
        f = (float)raw * c->scale;
        if (!(f > 0))
            return 0;
        return f >= PC_FT_COST_MAX ? PC_FT_COST_MAX : (int)lroundf(f);
    default:
        return raw;
    }
}

unsigned pc_ft_mask(const pc_ft_settings* c, const pc_ft_state* s, unsigned bits)
{
    if (pc_ft_whose(c, s) == PC_FT_PORT && c->cost == PC_FT_COST_FREE && s->circle)
        return bits & ~(unsigned)PC_FT_NO_MP;
    return bits;
}

unsigned pc_ft_menu_bits(const pc_ft_settings* c, const pc_ft_state* s)
{
    return pc_ft_whose(c, s) == PC_FT_PORT && s->circle ? 1u : 0u;
}

const char* pc_ft_blocked(const pc_ft_settings* c, const pc_ft_state* s)
{
    if (pc_ft_whose(c, s) != PC_FT_PORT)
        return NULL;
    if (s->warped)
        return "Leave the Room Browser warp first.";
    if (s->no_save)
        return "Cannot travel from here now.";
    if (s->here >= PC_FT_POINTS || !s->states[s->here])
        return "This Circle of Magic is not recorded yet.";
    return NULL;
}

/* The lowest bit of the menu mask MENU0 greys for, in the modern Magick
 * page's words (magick_bridge.c mask_reason). */
static const char* mask_why(unsigned m)
{
    if (m & 2)
        return "Not while carrying a cube.";
    if (m & 4)
        return "Not while hanging from a ledge.";
    if (m & PC_FT_NO_MP)
        return "Not enough MP.";
    if (m & 0x80)
        return "Silenced: spells cannot be cast.";
    return m ? "Not usable right now." : NULL;
}

const char* pc_ft_refused(const pc_ft_settings* c, const pc_ft_state* s, int point)
{
    const char* why;
    if (point < 0 || point >= PC_FT_USED || !s->states[point])
        return "Not found yet.";
    if (s->states[point] == 2)
        return PALING;
    if ((why = mask_why(s->mask & pc_ft_mask(c, s, PC_FT_MAGIC_MASK))))
        return why;
    if (s->mp < pc_ft_cost(c, s, pc_ft_raw_cost(s->here, point)))
        return "Not enough MP.";
    return NULL;
}

const char* pc_ft_shortcut(const pc_ft_settings* c, const pc_ft_state* s)
{
    int unlock = pc_ft_unlock(c, s), i;
    const char* why;
    if (!unlock)
        return c->enabled ? "Not yet: after the prologue." : "Fast travel is off.";
    if (unlock == 2)
        return PALING;
    if (!s->circle)
        return "Stand on a Circle of Magic.";
    if (s->event)
        return "Not during an event.";
    if (s->busy)
        return "Not now.";
    if ((why = pc_ft_blocked(c, s)))
        return why;
    if (s->here >= PC_FT_POINTS || !s->states[s->here])
        return "This Circle of Magic is not recorded yet.";
    for (i = 0; i < PC_FT_USED; ++i)
        if (i != s->here && !pc_ft_refused(c, s, i))
            return NULL;
    return "No other Circle of Magic to travel to.";
}

int pc_ft_counts_as_magic(const pc_ft_state* s) { return s->game != 0; }

int pc_ft_packed(int point, int cost)
{
    unsigned d = (unsigned)pc_ft_save_point_data[point];
    return (int)((d & 0x1FF) | ((d >> 16) & 0x1F) << 9 | ((d >> 24) & 0x1F) << 14
        | ((unsigned)cost & PC_FT_COST_MAX) << 19);
}

/* ---- the report ---- */

static const char* const whose_names[3] = { "off", "port", "game" };

#define PUT(...)                                                        \
    do {                                                                \
        int k = snprintf(out + n, (size_t)(size - n), __VA_ARGS__);     \
        if (k < 0 || k >= size - n)                                     \
            return -1;                                                  \
        n += k;                                                         \
    } while (0)

int pc_ft_json(const pc_ft_report* r, char* out, int size)
{
    const pc_ft_settings* c = &r->settings;
    const pc_ft_state* s = &r->state;
    const char* blocked = pc_ft_blocked(c, s);
    const char* why = pc_ft_shortcut(c, s);
    int n = 0, i;
    if (size <= 0)
        return -1;
    PUT("{\"settings\":{\"enabled\":%d,\"cost\":\"%s\",\"flat\":%d,\"scale\":%g,\"prompt\":%d},", c->enabled,
        c->cost >= 0 && c->cost < PC_FT_COST_MODES ? pc_ft_cost_names[c->cost] : "game", c->flat, (double)c->scale,
        r->prompt_on);
    PUT("\"whose\":\"%s\",\"unlock\":%d,\"menu_bits\":%u,\"prompt\":%s,\"prompt_drawn\":%u,",
        whose_names[pc_ft_whose(c, s)], pc_ft_unlock(c, s), pc_ft_menu_bits(c, s), r->prompt ? "true" : "false",
        r->prompt_drawn);
    PUT("\"state\":{\"game\":%d,\"intro\":%d,\"warped\":%d,\"no_save\":%d,\"circle\":%d,\"here\":%d,"
        "\"event\":%d,\"busy\":%d,\"mask\":%u,\"mp\":%d},",
        s->game, s->intro, s->warped, s->no_save, s->circle, s->here, s->event, s->busy, s->mask, s->mp);
    PUT("\"blocked\":\"%s\",\"why\":\"%s\",\"points\":[", blocked ? blocked : "", why ? why : "");
    for (i = 0; i < PC_FT_USED; ++i) {
        int raw = pc_ft_raw_cost(s->here, i);
        const char* refused = pc_ft_refused(c, s, i);
        PUT("%s{\"i\":%d,\"state\":%d,\"raw\":%d,\"cost\":%d,\"current\":%s,\"refused\":\"%s\"}", i ? "," : "", i,
            s->states[i], raw, pc_ft_cost(c, s, raw), raw == PC_FT_HERE ? "true" : "false", refused ? refused : "");
    }
    PUT("],\"presses\":%u,\"opened\":%u,\"ignored\":%u,", r->presses, r->opened, r->ignored);
    PUT("\"last\":{\"valid\":%s,\"from\":%d,\"to\":%d,\"location\":%d,\"whose\":\"%s\",\"counted\":%d,"
        "\"arrived\":%s,\"circle\":%d,\"room_ok\":%s,\"mp_before\":%d,\"mp_after\":%d}",
        r->last.valid ? "true" : "false", r->last.from, r->last.to, r->last.location,
        whose_names[r->last.whose >= 0 && r->last.whose < 3 ? r->last.whose : 0], r->last.counted,
        r->last.arrived ? "true" : "false", r->last.circle, r->last.room_ok ? "true" : "false", r->last.mp_before,
        r->last.mp_after);
    if (r->audited) {
        PUT(",\"audit\":[");
        for (i = 0; i < PC_FT_USED; ++i) {
            const pc_ft_audit_point* a = &r->audit[i];
            PUT("%s{\"i\":%d,\"zone\":%d,\"room\":%d,\"circle\":%d,\"cell\":[%d,%d,%d],\"ok\":%s}", i ? "," : "", i,
                a->zone, a->room, a->circle_id, a->cell[0], a->cell[1], a->cell[2], a->ok ? "true" : "false");
        }
        PUT("]");
    }
    PUT("}");
    return n;
}

void pc_ft_log_trip(const pc_ft_trip* t)
{
    fprintf(stderr, "[travel] Circle %d -> %d (location %#x), %s unlock%s\n", t->from, t->to, t->location,
        whose_names[t->whose >= 0 && t->whose < 3 ? t->whose : 0], t->counted ? ", counts as magic" : "");
}

void pc_ft_log_arrival(const pc_ft_trip* t)
{
    if (t->arrived)
        fprintf(stderr, "[travel] arrived on Circle %d (MP %d -> %d)\n", t->to, t->mp_before, t->mp_after);
    else
        fprintf(stderr, "[travel] arrival mismatch: Circle %d wanted, room %s, standing on %d\n", t->to,
            t->room_ok ? "right" : "wrong", t->circle - 1);
}
