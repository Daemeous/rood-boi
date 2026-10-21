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
 *     fast_travel.c's forced prompt.
 *   name_window: text box 7 with vs_battle_displaySceneMessage's text
 *     (D_800EB9D8, allocated there and freed by func_800CB50C; drawn as a band
 *     across the bottom of the screen, func_800CD158); the hook at its
 *     end (pc_hud_name_window) gives its message and action and starts its age;
 *     read each vsync (pc_hud_bridge_name_window, an event: never suppressed).
 *     Its show opens it (the battle's own closes it when its action ends; the
 *     field nothing: the hold keeps it, a show without hold is closed after its
 *     life, both only if it is still the one shown). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../widescreen.h"
#include "../ui/vs_encode.h"
#include "../classic/classic_parity.h"
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
void pc_hud_name_window(int message, int action);
extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int* D_800EB9D8; /* the name window's text (5BF94.c) */
extern char D_800F4E68; /* its countdown (func_800CB45C) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
static void classic_packets(const pc_hud_classic_cmd* cmds, int n, int anchor, int kind, int shadowed);
int pc_classic_walk_slots(const int* offsets, int count, int with_unk8, int enclosing, const void* first,
    const void* end, pc_classic_packet* out, int max); /* classic_bridge.c */
extern int pc_hw_anchor_recorded(int anchor) __attribute__((weak)); /* gpu_hw.c */
void _renderDigit(int font, int xy, int digit, u_long* before); /* 5BF94.c */

/* The ordering-table slot each kind's game drawing links into (bytes from vs_scratch.unk4): the status
 * words and the chain count -16 (func_800958A4, func_80095A4C), a buff +8 (func_8009695C), a number -20
 * (func_80094B0C), the gauges -12 (their bars, labels and separators; their digits -16). A classic draw
 * links its packets there too, unless it names another (d.layer). */
static const int kind_slot[PC_HUD_GAUGES + 1] = { 0, -16, -16, 8, -20, 0, 0, 0, -12 };
static const int gauge_slots[2] = { -12, -16 };
/* (the classic layer's kinds are the HUD layer's, by number) */
typedef char pc_hud_classic_kinds_agree[PC_CLASSIC_STATUS == PC_HUD_STATUS && PC_CLASSIC_CHAIN == PC_HUD_CHAIN
            && PC_CLASSIC_BUFF == PC_HUD_BUFF && PC_CLASSIC_DAMAGE == PC_HUD_DAMAGE && PC_CLASSIC_GAUGES == PC_HUD_GAUGES
        ? 1
        : -1];

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

/* The parity gate (classic_parity.h): the game's drawing of the element just recorded stays live; its
 * classic claim draws into a shadow slot (packets after the game's, never linked into the ordering
 * table, then given back), normalised and compared with the game's capture. */
static int lua_anchor;
static int shadow_anchor(const unsigned* tag, void* user)
{
    (void)tag, (void)user;
    return lua_anchor;
}
/* The parity mode's shadow slots: one word per slot a classic draw links into (its d.layer's), in place of
 * the ordering table's; walked in the table's drawing order (the higher slot first). */
#define SHADOW_SLOTS 8
static struct {
    int offset; /* bytes from vs_scratch.unk4 */
    u_long head;
} shadows[SHADOW_SLOTS];
static int shadow_count, shadowing;
static int slot_offset(int kind, int layer)
{
    return layer != PC_HUD_LAYER_KIND ? layer : kind_slot[kind >= 1 && kind <= PC_HUD_GAUGES ? kind : 1];
}
static u_long* slot_for(int kind, int layer)
{
    int offset = slot_offset(kind, layer), i;
    if (!shadowing)
        return (u_long*)((char*)psx_get(vs_scratch.unk4) + offset);
    for (i = 0; i < shadow_count; ++i)
        if (shadows[i].offset == offset)
            return &shadows[i].head;
    if (shadow_count == SHADOW_SLOTS)
        i = SHADOW_SLOTS - 1; /* (more slots than any draw uses: the last is shared) */
    else
        ++shadow_count;
    shadows[i].offset = offset;
    shadows[i].head = 0x00FFFFFF;
    return &shadows[i].head;
}
static void parity(int kind, int element_anchor)
{
    static pc_classic_packet lua[PC_CLASSIC_CAPTURE];
    const pc_hud_classic_cmd* cmds;
    const char* why = "no Lua";
    int count = pc_hud_classic_parity_draw ? pc_hud_classic_parity_draw(&cmds, &why) : -1, anchor, n = 0, i, j;
    void* start = psx_get(vs_scratch.unk0);
    pc_classic_walker walker = pc_classic_guest();
    if (count < 0) {
        pc_classic_parity_skip(kind, why);
        return;
    }
    anchor = pc_hud_classic_anchor && pc_hud_classic_anchor() >= 0 ? pc_hud_classic_anchor() : element_anchor;
    shadow_count = 0;
    shadowing = 1;
    classic_packets(cmds, count, anchor, kind, 1);
    shadowing = 0;
    lua_anchor = !pc_hw_anchor_recorded ? 0
        : pc_hw_anchor_recorded(anchor == PC_HUD_ANCHOR_LEFT ? PC_ANCHOR_LEFT_EDGE
                : anchor == PC_HUD_ANCHOR_RIGHT          ? PC_ANCHOR_RIGHT_EDGE
                                                         : PC_ANCHOR_CENTRE);
    walker.anchor = shadow_anchor;
    for (i = 1; i < shadow_count; ++i) /* (the table's drawing order: the higher slot first) */
        for (j = i; j > 0 && shadows[j].offset > shadows[j - 1].offset; --j) {
            int o = shadows[j].offset;
            u_long h = shadows[j].head;
            shadows[j] = shadows[j - 1];
            shadows[j - 1].offset = o, shadows[j - 1].head = h;
        }
    for (i = 0; i < shadow_count; ++i)
        n += pc_classic_walk(&walker, (const unsigned*)&shadows[i].head, start, psx_get(vs_scratch.unk0), lua + n,
            PC_CLASSIC_CAPTURE - n);
    pc_classic_parity_compare(kind, lua, n, pc_vsync_count ? pc_vsync_count() : 0);
    psx_set(vs_scratch.unk0, start); /* (given back: nothing links to them) */
}

/* After a hook's statement, its fields read: the capture (RPC packets, the gate), the record, then the
 * parity gate, the classic claim's draw (the game's packets suppressed) or the game's own drawing.
 * slots: the game's (bytes from vs_scratch.unk4), drawing order sorted by the walk; enclosing: the
 * anchor a caller gives the range after the hook (the gauges' panel, 146C.c). Returns pc_hud_record's. */
static int classic_finish(int kind, pc_hud_fields* f, void* first, void* end, const int* slots, int nslots,
    int enclosing)
{
    int amount;
    if (pc_classic_capturing()) { /* (RPC packets, the parity gate) */
        static pc_classic_packet game[PC_CLASSIC_CAPTURE], scanned[PC_CLASSIC_CAPTURE];
        int count = pc_classic_walk_slots(slots, nslots, 0, enclosing, first, end, game, PC_CLASSIC_CAPTURE);
        pc_classic_capture_game(kind, game, count, pc_vsync_count ? pc_vsync_count() : 0, f->text);
        pc_classic_walker guest = pc_classic_guest();
        pc_classic_capture_allocated(kind, pc_classic_scan(&guest, first, end, scanned, PC_CLASSIC_CAPTURE));
    }
    amount = pc_hud_record(f);
    if (pc_classic_parity_on() && !amount) {
        parity(kind, f->anchor);
        return 0;
    }
    if (amount == 2) { /* the classic flavour: the claim draws now, into packets of the game's kinds */
        const pc_hud_classic_cmd* cmds;
        int count = pc_hud_classic_draw ? pc_hud_classic_draw(&cmds) : -1;
        if (count < 0)
            return 0; /* it failed: the game's own drawing */
        pc_classic_suppress(first, end); /* (emit-only: the game's call ran, its packets are no-ops) */
        classic_packets(cmds, count, f->anchor, kind, 0);
    }
    return amount;
}

/* The gauges (hud.h PC_HUD_GAUGES): 5BF94.c's PC_CLASSIC around func_800CA2DC's _renderStatBar calls and
 * RISK ZERO; record the HUD record (vs_battle_actor2: currentHP .. risk). */
extern int D_800EB9B0; /* the status display's slide (5BF94.c) */
void pc_hud_gauges(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    vs_battle_actor2* actor = record;
    if (!actor || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_GAUGES;
    f.key = &f;
    f.hp = actor->currentHP, f.max_hp = actor->maxHP;
    f.mp = actor->currentMP, f.max_mp = actor->maxMP;
    f.risk = actor->risk;
    f.slide = D_800EB9B0;
    append(number(append(number(append(f.text, f.text + sizeof f.text, "HP "), f.text + sizeof f.text, f.hp),
                      f.text + sizeof f.text, " MP "),
               f.text + sizeof f.text, f.mp),
        f.text + sizeof f.text, f.risk ? " RISK" : " RISK ZERO");
    f.x = 50, f.y = 18, f.w = 140, f.h = 20; /* (the gauges' box, the panel's top-left) */
    f.rgb = 0xFFFFFF;
    f.anchor = PC_HUD_ANCHOR_LEFT;
    classic_finish(PC_HUD_GAUGES, &f, first, end, gauge_slots, 2, 1 /* the panel's left edge, 146C.c */);
}

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
    f.call_x = x, f.call_y = y;
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
    amount = classic_finish(kind, &f, first, end, &kind_slot[kind], 1, 0);
    if (amount == 1) /* the modern look: the claim draws over the picture */
        for (i = 0; i < n; ++i)
            q[i].packet[0] &= 0x00ffffff;
}

int pc_hud_bridge_field(void) { return status_field(); }

/* The name window: the scene message's hook (5BF94.c), what it opened. */
static struct {
    unsigned serial, ours; /* each opening; the one a show opened */
    unsigned opened;       /* vsync */
    int message, action;
} name;
void pc_hud_name_window(int message, int action)
{
    ++name.serial;
    name.opened = pc_vsync_count ? pc_vsync_count() : 0;
    name.message = message;
    name.action = action;
}
static int name_open(void) { return status_field() && D_800EB9D8 != NULL && vs_battle_textBoxes[7].state > 0; }

int pc_hud_bridge_name_window(pc_hud_fields* f)
{
    vs_battle_textBox* box = &vs_battle_textBoxes[7];
    unsigned now = pc_vsync_count ? pc_vsync_count() : 0;
    int i;
    if (!name_open())
        return 0;
    for (i = 0; i < (int)sizeof *f; ++i)
        ((char*)f)[i] = 0;
    f->kind = PC_HUD_NAME_WINDOW;
    f->key = &name;
    pc_ui_text_decode((const unsigned char*)D_800EB9D8, 112, f->text, sizeof f->text);
    if (box->unk0.unk0_0 == 4) { /* (the scene message's kind: func_800CD158's band across the screen's bottom,
                                  * or its top when the box is at 0, unk2A rows high) */
        f->w = 320, f->h = (float)box->unk2A;
        f->x = 160, f->y = box->y == 0 ? f->h * 0.5f : 240.0f - f->h * 0.5f;
    } else { /* a framed box: its rectangle as func_800CB83C scales it */
        f->x = (float)box->unk24 + (float)box->unk28 * 0.5f, f->y = (float)box->unk26 + (float)box->unk2A * 0.5f;
        f->w = (float)box->unk28, f->h = (float)box->unk2A;
    }
    f->rgb = 0xFFFFFF;
    f->variant = name.message;
    f->value = name.action;
    f->age = (int)(now - name.opened);
    f->anchor = PC_HUD_ANCHOR_CENTRE;
    return 1;
}

int pc_hud_bridge_spawn(int kind, const pc_hud_spawn* s)
{
    D_800F1BAC_t* p;
    if (kind == PC_HUD_NOTICE)
        return 0; /* the port's own: a show is a synthetic element */
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
    if (kind == PC_HUD_NAME_WINDOW) {
        if (!status_field())
            return 0;
        if (D_800EB9D8 != NULL)
            func_800CB50C(); /* (the one up goes, as the game's next one replaces it) */
        vs_battle_displaySceneMessage(s->variant, s->value & 0xFF, 1);
        name.ours = name.serial;
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
    if (kind == PC_HUD_NOTICE)
        return 0;
    if (kind == PC_HUD_TRAVEL)
        return pc_fast_travel_prompt_is_forced();
    if (kind == PC_HUD_NAME_WINDOW) {
        if (!name_open() || name.serial != name.ours)
            return 0;
        D_800F4E68 = 0x78; /* (a battle's countdown never runs out) */
        return 1;
    }
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
    if (kind == PC_HUD_NOTICE)
        return;
    if (kind == PC_HUD_TRAVEL) {
        pc_fast_travel_force_prompt(0);
        return;
    }
    if (kind == PC_HUD_NAME_WINDOW) {
        if (status_field() && D_800EB9D8 != NULL && name.serial == name.ours)
            func_800CB50C(); /* closed as the game closes it */
        name.ours = 0;
        return;
    }
    p = find(kind, kind == PC_HUD_BUFF || kind == PC_HUD_DAMAGE ? s : NULL);
    if (kind == PC_HUD_STATUS && p && p->unkB < 42)
        p->unkB = 42; /* its slide out */
    else if (kind == PC_HUD_CHAIN && p && p->unkB < 87)
        p->unkB = 87;
    /* (a buff or a number ages on from where it was held) */
}

/* ---- the classic flavour's packets (hud.h pc_hud_classic_cmd) ----
 *
 * Each command as packets of the kinds the game's own HUD builds, from the
 * packet pointer (vs_scratch.unk0), linked into the HUD labels' slot of the
 * ordering table (the status words' and digits': 4 entries before
 * vs_scratch.unk4). A packet linked at a slot's head draws before what the
 * slot held, so the commands go in last first.
 *   text     the info style's capitals (vs_battle_renderTextRawColor, as the
 *            status words and labels: its '$' centres, '#' right-aligns;
 *            the status style's font restyled as SLOW! is, pc_text_status),
 *            or the menus' variable-width font (dialogue, menu: vs_encode, a
 *            sprite per glyph as vs_battle_printVariableWidthFontChar sets it
 *            in font style 4, reported to ui_text, pc_text_glyph)
 *   rect, gradient   a tile, a Gouraud quad (alpha below 1: half blending)
 *   glyph    the field UI page's 16 x 16 button sprites (768, 0), CLUT
 *            (944, 223): button_prompts.c and button_roles.c redraw them as
 *            they do the game's
 *   sprite   any VRAM rectangle (its page and CLUT words)
 *   window   the text boxes' window (func_800CD3E4 for a rectangle): its
 *            shadow 2 pixels down and right, its body (page (448, 256), CLUT
 *            (992, 220), a 64-texel tile: texture window 0x318), its frame
 *            lines 0x303030
 *   cursor   vs_battle_drawCursor's hand at a place (brightness from
 *            vs_battle_cursorBrightnessAnimation)
 *   bar      vs_battle_renderStatBar (HP, MP, RISK) */
extern u_char D_800EB708[], D_800EB7B4[]; /* the info capitals' kerning and widths (5BF94.c) */
extern void pc_text_status(const void* first, const void* end) __attribute__((weak)); /* ui_text.c */
extern void pc_text_status_typeset(const void* first, const void* end, const pc_text_typeset* typeset)
    __attribute__((weak));
extern void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance)
    __attribute__((weak));

static int restyle = 1; /* text reported to ui_text (not for the parity gate's shadow packets) */

/* n words after a tag at the packet pointer, linked at the head of ot. */
static u_long* packet(u_long* ot, int n)
{
    u_long* p = psx_get(vs_scratch.unk0);
    p[0] = (*ot & 0xFFFFFF) | ((u_long)n << 24);
    *ot = (psx_to_int(u_long, p) << 8) >> 8;
    psx_set(vs_scratch.unk0, p + 1 + n);
    return p + 1;
}

/* 0xRRGGBB as a PS1 colour word (BGR): direct (tiles), or modulating a texture (0xFF -> 0x80). */
static u_long bgr(unsigned rgb) { return ((rgb >> 16) & 255) | (rgb & 0xFF00) | ((rgb & 255) << 16); }
static u_long modulate(unsigned rgb)
{
    unsigned r = ((rgb >> 16) & 255) + 1, g = ((rgb >> 8) & 255) + 1, b = (rgb & 255) + 1;
    return (r >> 1) | ((g >> 1) << 8) | ((b >> 1) << 16);
}
static u_long xy(int x, int y) { return (u_long)(x & 0xFFFF) | ((u_long)(y & 0xFFFF) << 16); }
static int semi(float alpha) { return alpha < 0.99f; }

/* The info capitals' advance of text (as vs_battle_renderTextRawColor's centring measures). */
static int info_width(const char* text, int length)
{
    int width = 0, i;
    for (i = 0; i < length; ++i) {
        u_int ch = (u_char)text[i] - 0x20;
        if (ch < 0x3C) {
            width += (D_800EB7B4[ch >> 1] >> ((ch << 2) & 4)) & 0xF;
            if (ch - 0x21 < 26 && i + 1 < length && (u_int)((u_char)text[i + 1] - 0x41) < 26) {
                u_int index = (u_char)text[i + 1] - 0x41 + (ch - 0x21) * 26;
                width -= (D_800EB708[index >> 2] >> ((index & 3) * 2)) & 3;
            }
        }
    }
    return width;
}
static int menu_font(int font) { return font == 0 || font == 1; } /* PC_UI_TEXT_DIALOGUE, _MENU */
#define MENU_SPACE 0x8F

static int encode(const char* text, int length, u_char* out, int cap)
{
    char utf8[260];
    int n = length < (int)sizeof utf8 - 1 ? length : (int)sizeof utf8 - 1, i;
    for (i = 0; i < n; ++i)
        utf8[i] = text[i];
    utf8[n] = 0;
    return vs_encode(utf8, out, cap, NULL) - 1; /* (without its 0xE7) */
}
static int menu_width(const u_char* glyphs, int n)
{
    int w = 0, i;
    for (i = 0; i < n; ++i)
        w += glyphs[i] == MENU_SPACE ? 6 : glyphs[i] < 0xE7 ? vs_battle_characterWidths[glyphs[i]] : 0;
    return w;
}

void pc_hud_classic_measure(int font, const char* text, int length, int* w, int* h)
{
    if (menu_font(font)) {
        u_char glyphs[260];
        int n = encode(text, length, glyphs, sizeof glyphs);
        *w = menu_width(glyphs, n > 0 ? n : 0);
        *h = 12;
    } else {
        *w = info_width(text, length);
        *h = 10;
    }
}

/* The field UI page's button cells (PC_PROMPT_* order: L2, R2, L1, R1, triangle, circle, cross,
 * square; the rest have none): u, v. */
static const signed char button_cells[8][2] = { { 32, 16 }, { 48, 16 }, { 0, 16 }, { 16, 16 }, { 0, 0 }, { 32, 0 },
    { 48, 0 }, { 16, 0 } };
int pc_hud_classic_glyph(int button) { return button >= 0 && button < 8 ? 16 : 0; }

/* A sprite as the game's (pc_sprite_packet): its page word (semi-transparent: the page's blending), its
 * colour and command word (returned), place, texels and CLUT, size. */
static u_long* sprite(u_long* ot, int x, int y, int w, int h, u_long page, u_long uv_clut, unsigned rgb, float alpha)
{
    u_long* p = packet(ot, 5);
    p[0] = 0xE1000000 | page;
    p[1] = modulate(rgb) | ((u_long)(semi(alpha) ? 0x66 : 0x64) << 24);
    p[2] = xy(x, y);
    p[3] = uv_clut;
    p[4] = xy(w, h);
    return p + 1;
}

static void text_packets(u_long* ot, const pc_hud_classic_cmd* c)
{
    int w, h, x = c->x, y = c->y, align = c->align;
    pc_hud_classic_measure(c->font, c->text, c->length, &w, &h);
    if (c->fit) { /* inside the box: across by the align, the middle on its middle */
        x = align == 1 ? c->x + c->w / 2 : align == 2 ? c->x + c->w : c->x;
        y = c->y + (c->h - h) / 2;
    } else {
        y -= c->anchor == 1 ? h / 2 : c->anchor == 2 ? h - 2 : c->anchor == 3 ? h : 0;
    }
    if (menu_font(c->font)) {
        u_char glyphs[260];
        u_long* command[260];
        int n = encode(c->text, c->length, glyphs, sizeof glyphs), i, pen;
        if (n <= 0)
            return;
        x -= align == 1 ? w / 2 : align == 2 ? w : 0;
        /* linked last first (the slot draws them first to last), reported to ui_text first to last */
        for (i = n - 1, pen = x + w; i >= 0; --i) {
            u_int glyph = glyphs[i];
            pen -= glyph == MENU_SPACE ? 6 : glyph < 0xE7 ? vs_battle_characterWidths[glyph] : 0;
            command[i] = glyph < 0xE7 && glyph != MENU_SPACE
                ? sprite(ot, pen, y, 12, 12, 0x2D, /* font style 4: CLUT (896, 222) */
                      ((glyph % 21) * 12) | ((glyph / 21) * 12 << 8) | 0x37B80000u, c->color, 0.5f)
                : NULL;
        }
        for (i = 0, pen = x; i < n && pc_text_glyph && restyle; ++i) {
            u_int glyph = glyphs[i];
            int advance = glyph == MENU_SPACE ? 6 : glyph < 0xE7 ? vs_battle_characterWidths[glyph] : 0;
            if (command[i])
                pc_text_glyph((void* const*)&command[i], 1, (int)glyph, pen << 16, y << 16, (pen + advance) << 16,
                    advance);
            else if (glyph == MENU_SPACE)
                pc_text_glyph(NULL, 0, -1, pen << 16, y << 16, (pen + 6) << 16, 6);
            pen += advance;
        }
        return;
    }
    {
        char ascii[260];
        int n = 0, i;
        void* first = psx_get(vs_scratch.unk0);
        if (align == 1 || align == 2)
            ascii[n++] = align == 1 ? '$' : '#';
        for (i = 0; i < c->length && n < (int)sizeof ascii - 1; ++i) {
            char ch = c->text[i];
            ascii[n++] = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; /* (capitals only) */
        }
        ascii[n] = 0;
        vs_battle_renderTextRawColor(ascii, xy(x, y), (int)modulate(c->color), ot);
        if (c->font == 4 && pc_text_status && restyle) /* PC_UI_TEXT_STATUS: set as the status words are */
            pc_text_status(first, psx_get(vs_scratch.unk0));
    }
}

static void window_packets(u_long* ot, const pc_hud_classic_cmd* c)
{
    int x0 = c->x, y0 = c->y, x1 = c->x + c->w, y1 = c->y + c->h, ax = x0 & ~63, ay = y0 & ~63, x, y;
    u_long grey = (u_long)(c->a < 0 ? 0 : c->a > 255 ? 255 : c->a), *p;
    u_long corners[4] = { xy(x0, y0), xy(x1, y0), xy(x1, y1), xy(x0, y1) };
    int i;
    grey |= grey << 8 | grey << 16;
    /* (last drawn first) the frame */
    for (i = 3; i >= 0; --i) {
        p = packet(ot, 3);
        p[0] = 0x40303030;
        p[1] = corners[i];
        p[2] = corners[(i + 1) & 3];
    }
    p = packet(ot, 1);
    p[0] = 0xE2000000; /* the texture window back */
    /* the body, in pieces the texels' 8 bits span (the tile repeats every 64) */
    for (x = x0; x < x1; x += 128)
        for (y = y0; y < y1; y += 128) {
            int xe = x + 128 < x1 ? x + 128 : x1, ye = y + 128 < y1 ? y + 128 : y1;
            int u0 = (x - ax) & 63, u1 = u0 + xe - x, v0 = (y - ay) & 63, v1 = v0 + ye - y;
            p = packet(ot, 9);
            p[0] = grey | 0x2C000000;
            p[1] = xy(x, y);
            p[2] = (u_long)u0 | ((u_long)v0 << 8) | 0x373E0000;
            p[3] = xy(xe, y);
            p[4] = (u_long)u1 | ((u_long)v0 << 8) | 0x170000;
            p[5] = xy(x, ye);
            p[6] = (u_long)u0 | ((u_long)v1 << 8);
            p[7] = xy(xe, ye);
            p[8] = (u_long)u1 | ((u_long)v1 << 8);
        }
    p = packet(ot, 2);
    p[0] = 0xE1000017;
    p[1] = 0xE2000318; /* its 64-texel tile */
    /* the shadow, 2 pixels down and right: filled, then outlined */
    p = packet(ot, 12);
    p[0] = 0x28000000;
    p[1] = xy(x0 + 2, y0 + 2);
    p[2] = xy(x1 + 2, y0 + 2);
    p[3] = xy(x0 + 2, y1 + 2);
    p[4] = xy(x1 + 2, y1 + 2);
    p[5] = 0x48000000;
    p[6] = p[1];
    p[7] = p[2];
    p[8] = p[4];
    p[9] = p[3];
    p[10] = p[1];
    p[11] = 0x55555555;
}

static void quad_packets(u_long* ot, const pc_hud_classic_cmd* c)
{
    u_long* p;
    int x1 = c->x + c->w, y1 = c->y + c->h;
    if (c->w <= 0 || c->h <= 0)
        return;
    if (semi(c->alpha)) {
        p = packet(ot, 1);
        p[0] = 0xE1000000; /* (after it: the draw mode the HUD's own packets set) */
    }
    if (c->type == PC_HUD_CMD_RECT) {
        p = packet(ot, 3);
        p[0] = bgr(c->color) | ((u_long)(semi(c->alpha) ? 0x62 : 0x60) << 24);
        p[1] = xy(c->x, c->y);
        p[2] = xy(c->w, c->h);
    } else {
        u_long a = bgr(c->color), b = bgr(c->color2);
        p = packet(ot, 8);
        p[0] = a | ((u_long)(semi(c->alpha) ? 0x3A : 0x38) << 24);
        p[1] = xy(c->x, c->y);
        p[2] = c->a ? a : b;
        p[3] = xy(x1, c->y);
        p[4] = c->a ? b : a;
        p[5] = xy(c->x, y1);
        p[6] = b;
        p[7] = xy(x1, y1);
    }
    if (semi(c->alpha)) {
        p = packet(ot, 1);
        p[0] = 0xE1000000; /* half blending (abr 0) for it */
    }
}

/* The commands as packets in the kind's slot (shadow: into that word instead, for the parity gate:
 * nothing reported to ui_text or widescreen), the range anchored exactly (PC_ANCHOR_EXACT) where
 * `anchor` says (PC_HUD_ANCHOR_*; d.anchor's, else the element's). */
static void classic_packets(const pc_hud_classic_cmd* cmds, int n, int anchor, int kind, int shadowed)
{
    u_long* ot;
    void* start = psx_get(vs_scratch.unk0);
    int i;
    restyle = !shadowed;
    if (!shadowed && pc_hud_classic_anchor && pc_hud_classic_anchor() >= 0)
        anchor = pc_hud_classic_anchor();
    for (i = n - 1; i >= 0; --i) {
        const pc_hud_classic_cmd* c = &cmds[i];
        ot = slot_for(kind, c->layer);
        if (c->alpha <= 0.01f && c->type != PC_HUD_CMD_WINDOW && c->type != PC_HUD_CMD_BAR && c->type != PC_HUD_CMD_CURSOR)
            continue;
        switch (c->type) {
        case PC_HUD_CMD_TEXT:
            text_packets(ot, c);
            break;
        case PC_HUD_CMD_VALUE:
            vs_battle_renderValue(c->a, (int)xy(c->x, c->y), c->b, ot);
            break;
        case PC_HUD_CMD_DIGIT:
            _renderDigit(c->a, (int)xy(c->x, c->y), c->b, ot);
            break;
        case PC_HUD_CMD_TEXT_RAW: { /* as the game calls it (func_800958A4: its string, place, colour word) */
            void* first = psx_get(vs_scratch.unk0);
            vs_battle_renderTextRawColor(c->text, (int)xy(c->x, c->y), c->a, ot);
            if (c->font == 4 && restyle && pc_text_status_typeset)
                pc_text_status_typeset(first, psx_get(vs_scratch.unk0), &c->typeset);
            break;
        }
        case PC_HUD_CMD_RECT:
        case PC_HUD_CMD_GRADIENT:
            quad_packets(ot, c);
            break;
        case PC_HUD_CMD_GLYPH:
            if (c->a >= 0 && c->a < 8)
                sprite(ot, c->x, c->y - 8, 16, 16, 0x0C,
                    (u_long)button_cells[c->a][0] | ((u_long)(128 + button_cells[c->a][1]) << 8) | 0x37FB0000u, c->color,
                    c->alpha);
            break;
        case PC_HUD_CMD_SPRITE:
            sprite(ot, c->x, c->y, c->w, c->h, (u_long)c->a, (u_long)(c->c & 255) | ((u_long)(c->d & 255) << 8)
                | ((u_long)(c->b & 0xFFFF) << 16), c->color, c->alpha);
            break;
        case PC_HUD_CMD_WINDOW:
            window_packets(ot, c);
            break;
        case PC_HUD_CMD_CURSOR: {
            int step = c->a >= 0 ? c->a : pc_vsync_count ? (int)(pc_vsync_count() >> 1) & 15 : 0;
            u_long brightness = (u_char)vs_battle_cursorBrightnessAnimation[step];
            *sprite(ot, c->x, c->y, 16, 16, 0x0C, 0x37F83020u, 0xFFFFFF, 1) = brightness * 0x010101u | 0x64000000u;
            break;
        }
        case PC_HUD_CMD_BAR: {
            int fill = (int)(((long long)(c->a < c->b ? c->a : c->b) * 64 + c->b - 1) / c->b);
            vs_battle_renderStatBar(c->c, fill, ot, (int)xy(c->x, c->y));
            break;
        }
        }
    }
    restyle = 1;
    if (!shadowed && pc_hw_anchor_packets) /* placed by data: exactly, no heuristics */
        pc_hw_anchor_packets(start, psx_get(vs_scratch.unk0),
            (anchor == PC_HUD_ANCHOR_LEFT ? PC_ANCHOR_LEFT_EDGE
                    : anchor == PC_HUD_ANCHOR_RIGHT ? PC_ANCHOR_RIGHT_EDGE
                                                    : PC_ANCHOR_CENTRE)
                | PC_ANCHOR_EXACT);
}
