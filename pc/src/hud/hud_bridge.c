/* HUD elements, the game side (hud.h; battle builds, compiled with the game
 * headers): src's PC_HUD hook (hud_hook.h) reads the game's record into a
 * pc_hud_fields copy and, when hud_elements.cpp says a claim is honoured,
 * turns the element's quads into no-ops (their packets' length 0: the links
 * stay, the GPU skips them; draw-mode packets kept, so nothing after them
 * changes). rood.dev.show's moments: the game's own spawner and the hold
 * (the record's age pinned each vsync).
 *
 * The game's labels (D_800F1BAC_t, BATTLE.PRG 2842C.c func_800941FC's loop):
 *   status: func_800958A4 from case 4 (TOO FAST!, FAST!, SLOW!, TOO SLOW!, or
 *     COOL!, GOOD!, ... for a well-timed chain): unkC the timing,
 *     displayTextLen the success word, unkB the age; gone at 46. Sprites.
 *   chain: func_80095A4C from case 5 ("3 CHAINS"): unkC the count; gone at 91.
 *   buff: func_8009695C from case 6 (kind 7 waits for the equipment menu to
 *     close, func_800971D4): unkE the equipment (D_80069144's words), unkA
 *     the class or affinity (D_8006917C's), unkC the amount (0x8000 a loss),
 *     unk6 its place in the queue (shown below 0), unk12 the rows it rose;
 *     gone at 61. Textured quads: the two words, the sign, the digits.
 *   damage: func_80094B0C from case 1: u8[1] the actor, unkE 0 HP, 1 MP, 3..8
 *     a stat change (its icon: HP, MP, STR, INT, AGL, RISK, and a sign first:
 *     func_80094E18; the word named from ui_labels.h), unkC the amount
 *     (0x8000 damage, else healing), the digits in displayText; gone at 31.
 *     Textured quads.
 *   travel: not the game's (fast_travel_prompt.cpp records it); its show is
 *     fast_travel.c's forced prompt. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "hud.h"


extern short D_800F227E; /* the labels in D_800F1BAC */
int vs_battle_renderBattleAbilityTimingResult(int arg0);
int func_80093F24(int count);                              /* chain */
int func_80093B68(int actor, int limb, int amount, int kind); /* damage */
int func_80096768(int slot, int stat, int amount);        /* buff (kind 7) */
void func_800971D4(void);                                  /* kind 7 -> 6 */
void pc_fast_travel_force_prompt(int on);                  /* fast_travel.c */
const char* pc_ui_text_label_at(int page_x, int page_y, int u0, int v0, int u1, int v1) __attribute__((weak));
int pc_fast_travel_prompt_is_forced(void);

void pc_hud_element(int kind, void* record, int x, int y, void* first, void* end);

#define STATUS_LIFE 46
#define CHAIN_LIFE 91
#define BUFF_LIFE 61
#define DAMAGE_LIFE 31

static int status_field(void)
{
    return vs_battle_roomData.unk0 == 1 && vs_battle_characterState != NULL && D_800F1BAC != NULL
        && D_800F227E >= 0 && D_800F227E <= 32;
}

/* func_80096768's equipment codes by D_80069144's word (its switch, inverted). */
static const int slot_codes[7] = { 240, 241, 2, 3, 1, 6, 4 };
static const char* const slot_words[7] = { "WEAPON", "SHIELD", "R.ARM", "L.ARM", "HEAD", "BODY", "LEGS" };
static const char* const stat_words[13] = { "HUMAN", "BEAST", "UNDEAD", "PHANTOM", "DRAGON", "EVIL", "PHYSICS", "AIR",
    "FIRE", "EARTH", "WATER", "LIGHT", "DARKNESS" };

/* (the game's headers stand in for libc: plain helpers) */
static char* append(char* out, char* end, const char* s)
{
    while (*s && out < end - 1)
        *out++ = *s++;
    *out = 0;
    return out;
}
static char* number(char* out, char* end, int v)
{
    char digits[12];
    int n = 0;
    if (v < 0) {
        out = append(out, end, "-");
        v = -v;
    }
    do
        digits[n++] = (char)('0' + v % 10);
    while ((v /= 10) && n < 11);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

static int amount_of(const pc_hud_spawn* s) { return (s->value & 0x7FFF) | (s->sign < 0 ? 0x8000 : 0); }

/* The game's record of kind: status and chain are one at a time; a buff or a damage
 * number is the one shown (s: the one spawned; NULL: the first), not one that is ending. */
static D_800F1BAC_t* find(int kind, const pc_hud_spawn* s)
{
    D_800F1BAC_t* p;
    int i;
    if (!status_field())
        return NULL;
    p = *D_800F1BAC;
    for (i = 0; i < D_800F227E; ++i, ++p) {
        switch (kind) {
        case PC_HUD_STATUS:
            if (p->unk8.u16 == 4)
                return p;
            break;
        case PC_HUD_CHAIN:
            if (p->unk8.u16 == 5)
                return p;
            break;
        case PC_HUD_BUFF:
            if ((p->unk8.u8[0] == 6 || p->unk8.u8[0] == 7) && p->unkB < BUFF_LIFE
                && (!s || (p->unkE == s->slot && p->unkA == s->stat && p->unkC == amount_of(s))))
                return p;
            break;
        case PC_HUD_DAMAGE:
            if (p->unk8.u8[0] == 1 && p->unk8.u8[1] == 0 && p->unkB < DAMAGE_LIFE
                && (!s || ((p->unkC & 0xFFFF) == amount_of(s) && (p->unkE & 0x7F) == (s->pool == 2 ? 3 + s->stat
                                                                                          : s->pool))))
                return p;
            break;
        }
    }
    return NULL;
}

/* A PS1 colour word's BGR at full brightness (0x80 -> 0xFF) as 0xRRGGBB. */
static unsigned full_rgb(unsigned bgr)
{
    unsigned out = 0, v;
    int i;
    for (i = 0; i < 3; ++i) {
        v = (bgr >> (8 * i)) & 255u;
        v = v * 2 > 255 ? 255 : v * 2;
        out |= v << (8 * (2 - i));
    }
    return out;
}

/* The packets [first, end) the hook saw: its quads (sprites GP0 0x64-0x67, textured
 * four-point polygons 0x2C-0x2F) in drawing order. */
#define MAX_QUADS 16
typedef struct {
    u_int* packet;
    int x0, y0, x1, y1;
    unsigned rgb;
} hud_quad;

static int quads(void* first, void* end, hud_quad* out)
{
    u_int* w = first;
    u_int* stop = end;
    int count = 0;
    if (!w || !stop || stop <= w || stop - w > 10 * 64)
        return 0;
    while (w + 1 < stop && count < MAX_QUADS) {
        u_int len = w[0] >> 24, code = w[1] >> 24;
        hud_quad* q = &out[count];
        if (len == 4 && (code & 0xfc) == 0x64 && w + 5 <= stop) {
            int x = (short)(w[2] & 0xffff), y = (short)(w[2] >> 16);
            q->x0 = x, q->y0 = y, q->x1 = x + (int)(w[4] & 0xffff), q->y1 = y + (int)(w[4] >> 16);
        } else if (len == 9 && (code & 0xfc) == 0x2c && w + 10 <= stop) {
            int k;
            q->x0 = q->y0 = 1 << 20, q->x1 = q->y1 = -(1 << 20);
            for (k = 0; k < 4; ++k) {
                int x = (short)(w[2 + 2 * k] & 0xffff), y = (short)(w[2 + 2 * k] >> 16);
                q->x0 = x < q->x0 ? x : q->x0, q->y0 = y < q->y0 ? y : q->y0;
                q->x1 = x > q->x1 ? x : q->x1, q->y1 = y > q->y1 ? y : q->y1;
            }
        } else {
            w += 1 + len;
            continue;
        }
        q->packet = w;
        q->rgb = full_rgb(w[1] & 0xffffff);
        ++count;
        w += 1 + len;
    }
    return count;
}

/* The union of quads [a, b) as a part (and its text). */
static void part(pc_hud_fields* f, const hud_quad* q, int a, int b, const char* text)
{
    pc_hud_part* p;
    int i, x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    if (a >= b || f->part_count >= PC_HUD_PARTS)
        return;
    for (i = a; i < b; ++i) {
        x0 = q[i].x0 < x0 ? q[i].x0 : x0, y0 = q[i].y0 < y0 ? q[i].y0 : y0;
        x1 = q[i].x1 > x1 ? q[i].x1 : x1, y1 = q[i].y1 > y1 ? q[i].y1 : y1;
    }
    p = &f->parts[f->part_count++];
    p->x = (float)x0, p->y = (float)y0, p->w = (float)(x1 - x0), p->h = (float)(y1 - y0);
    append(p->text, p->text + sizeof p->text, text);
}

/* f's box: every quad's; x, y its centre. */
static void box(pc_hud_fields* f, const hud_quad* q, int n)
{
    int i, x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (i = 0; i < n; ++i) {
        x0 = q[i].x0 < x0 ? q[i].x0 : x0, y0 = q[i].y0 < y0 ? q[i].y0 : y0;
        x1 = q[i].x1 > x1 ? q[i].x1 : x1, y1 = q[i].y1 > y1 ? q[i].y1 : y1;
    }
    f->x = (float)(x0 + x1) * 0.5f, f->y = (float)(y0 + y1) * 0.5f;
    f->w = (float)(x1 - x0), f->h = (float)(y1 - y0);
}

/* Each word as func_800958A4 prints it (its strings' $ and wide spaces left out). */
static const char* const timing_words[] = { "TOO FAST!", "FAST!", "", "SLOW!", "TOO SLOW!" };
static const char* const success_words[] = { "COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!",
    "WELL TIMED!", "NICE!", "GREAT!" };
/* func_80094B0C's bounce, by frame. */
static const signed char bounce[] = { 0, -6, -12, -14, -16, -10, -5, -1, 0, -3, -6, -7, -8, -5, -3, -1, 0, -2, -1, 0 };

void pc_hud_element(int kind, void* record, int x, int y, void* first, void* end)
{
    static pc_hud_fields f; /* (reused: no stack churn of a 200-byte record per label) */
    hud_quad q[MAX_QUADS];
    D_800F1BAC_t* p = record;
    int n, i, amount;
    char digits[8], *at, *text_end = f.text + sizeof f.text;
    if (!p || kind < PC_HUD_STATUS || kind > PC_HUD_DAMAGE)
        return;
    n = quads(first, end, q);
    if (!n)
        return; /* nothing drawn (off the screen) */
    f = (pc_hud_fields) { 0 };
    f.kind = kind;
    f.key = p;
    f.age = p->unkB;
    f.rgb = q[0].rgb;
    box(&f, q, n);
    switch (kind) {
    case PC_HUD_STATUS:
        f.timing = p->unkC < 5 ? p->unkC : 0;
        f.variant = p->displayTextLen & 7;
        append(f.text, text_end, f.timing == 2 ? success_words[f.variant] : timing_words[f.timing]);
        f.life = STATUS_LIFE;
        f.anchor = PC_HUD_ANCHOR_LEFT;
        break;
    case PC_HUD_CHAIN:
        f.value = p->unkC;
        append(number(f.text, text_end, f.value), text_end, f.value == 1 ? " CHAIN" : " CHAINS");
        f.life = CHAIN_LIFE;
        f.anchor = PC_HUD_ANCHOR_LEFT;
        break;
    case PC_HUD_BUFF:
        f.slot = p->unkE >= 0 && p->unkE < 7 ? p->unkE : 0;
        f.stat = (u_char)p->unkA < 13 ? (u_char)p->unkA : 0;
        f.sign = p->unkC & 0x8000 ? -1 : 1;
        f.value = p->unkC & 0x7FFF;
        number(digits, digits + sizeof digits, f.value);
        at = append(append(append(f.text, text_end, slot_words[f.slot]), text_end, " "), text_end, stat_words[f.stat]);
        append(append(at, text_end, f.sign < 0 ? " -" : " +"), text_end, digits);
        f.life = BUFF_LIFE;
        f.anchor = PC_HUD_ANCHOR_LEFT;
        part(&f, q, 0, 1, slot_words[f.slot]);
        part(&f, q, 1, 2, stat_words[f.stat]);
        part(&f, q, 2, 3, f.sign < 0 ? "-" : "+");
        part(&f, q, 3, n, digits);
        break;
    case PC_HUD_DAMAGE:
        amount = p->unkC & 0xFFFF;
        f.value = amount & 0x7FFF;
        f.sign = amount & 0x8000 ? -1 : 1;
        i = p->unkE & 0x7F;
        f.pool = i >= 3 ? 2 : i & 1;
        f.stat = i >= 3 ? i - 3 : 0;
        number(f.text, text_end, f.value);
        if (f.pool == 2) {
            /* its icon's word (func_80094E18's image: STR, INT, AGL, RISK, ...) */
            u_int* w = q[0].packet;
            int tpage = (int)(w[5] >> 16), u0 = (int)(w[3] & 0xff), v0 = (int)(w[3] >> 8 & 0xff);
            int u1 = (int)(w[9] & 0xff), v1 = (int)(w[9] >> 8 & 0xff);
            const char* word = pc_ui_text_label_at && (w[0] >> 24) == 9
                ? pc_ui_text_label_at((tpage & 15) * 64, (tpage >> 4 & 1) * 256, u0, v0, u1 - 1, v1 - 1)
                : NULL;
            f.bounce = (float)(f.sign > 0 ? -(p->unkB >> 1) : p->unkB >> 1); /* a drift */
            part(&f, q, 0, 1, word ? word : "");
            part(&f, q, 1, 2, f.sign < 0 ? "-" : "+");
            part(&f, q, 2, n, f.text);
        } else {
            int frame = p->unkB < 16 ? p->unkB : 15;
            f.bounce = (float)(f.sign > 0 ? bounce[frame] : bounce[frame + 4]);
            part(&f, q, 0, n, f.text);
        }
        f.x = (float)x, f.y = (float)y;
        f.rgb = q[n - 1].rgb; /* the digits' (a stat's icon is drawn in its own colours) */
        f.life = DAMAGE_LIFE;
        f.anchor = PC_HUD_ANCHOR_CENTRE;
        break;
    }
    if (pc_hud_record(&f))
        for (i = 0; i < n; ++i)
            q[i].packet[0] &= 0x00ffffff;
}

int pc_hud_bridge_field(void) { return status_field(); }

int pc_hud_bridge_spawn(int kind, const pc_hud_spawn* s)
{
    D_800F1BAC_t* p;
    /* (a buff or a number the same as the one asked for ends at once: the new one is then the
     * one found, and none waits behind it in the actor's queue) */
    while ((kind == PC_HUD_BUFF || kind == PC_HUD_DAMAGE) && (p = find(kind, s)) != NULL) {
        p->unkB = kind == PC_HUD_BUFF ? BUFF_LIFE : DAMAGE_LIFE;
        p->unk6 = -1;
    }
    if (kind == PC_HUD_TRAVEL) {
        pc_fast_travel_force_prompt(1);
        return 1;
    }
    if (!status_field() || D_800F227E >= 30) /* (the game's pool holds 32) */
        return 0;
    switch (kind) {
    case PC_HUD_STATUS:
        vs_battle_renderBattleAbilityTimingResult(s->timing);
        p = find(kind, NULL);
        if (!p)
            return 0;
        p->displayTextLen = (char)s->variant; /* (the spawner picks one at random) */
        return 1;
    case PC_HUD_CHAIN:
        func_80093F24(s->value);
        return find(kind, NULL) != NULL;
    case PC_HUD_BUFF: {
        /* (shown only with the game's option for that equipment on: on for the call) */
        int weapon = vs_main_settings.weaponStatChange, armor = vs_main_settings.armorStatChange;
        vs_main_settings.weaponStatChange = 1;
        vs_main_settings.armorStatChange = 1;
        func_80096768(slot_codes[s->slot], s->stat, (s->value & 0x7FFF) | (s->sign < 0 ? (int)0x80000000 : 0));
        vs_main_settings.weaponStatChange = weapon;
        vs_main_settings.armorStatChange = armor;
        func_800971D4(); /* (as when the equipment menu closes) */
        p = find(kind, s);
        if (!p)
            return 0;
        p->unk6 = -1; /* now, not after the ones queued before it */
        return 1;
    }
    case PC_HUD_DAMAGE: {
        int amount = (s->value & 0x7FFF) | (s->sign < 0 ? (int)0x80000000 : 0);
        int type = s->pool == 2 ? 3 + s->stat : s->pool;
        /* on Ashley: her body's first part, else her centre */
        if (func_80093B68(0, 0, amount, type) != 0 && func_80093B68(0, -1, amount, type) != 0)
            return 0;
        return find(kind, s) != NULL;
    }
    }
    return 0;
}

int pc_hud_bridge_hold(int kind, const pc_hud_spawn* s)
{
    D_800F1BAC_t* p;
    if (kind == PC_HUD_TRAVEL)
        return pc_fast_travel_prompt_is_forced();
    p = find(kind, kind == PC_HUD_BUFF || kind == PC_HUD_DAMAGE ? s : NULL);
    if (!p)
        return 0;
    p->unkB = (char)s->age;
    if (kind == PC_HUD_BUFF)
        p->unk6 = -1;
    return 1;
}

void pc_hud_bridge_release(int kind, const pc_hud_spawn* s)
{
    D_800F1BAC_t* p;
    if (kind == PC_HUD_TRAVEL) {
        pc_fast_travel_force_prompt(0);
        return;
    }
    p = find(kind, kind == PC_HUD_BUFF || kind == PC_HUD_DAMAGE ? s : NULL);
    if (kind == PC_HUD_STATUS && p && p->unkB < 42)
        p->unkB = 42; /* its slide out */
    else if (kind == PC_HUD_CHAIN && p && p->unkB < 87)
        p->unkB = 87;
    /* (a buff or a number ages on from where it was held) */
}
