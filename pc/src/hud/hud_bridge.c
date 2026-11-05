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
#include "../classic/classic_hook.h"
#include "../../../src/include/gpu.h" /* (vs_mainmenu_renderButtonBackground's packets) */
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
 * (func_80094B0C) and a word over an actor -20, the timer -4 (_renderTimer), an emote its own (its actor's
 * depth: element_slot) (func_8009506C, func_80095258, its arrow func_800957D0), the
 * gauges -12 (their bars, labels and separators; their digits -16), a target row its own (vs_scratch.unk8 + 8, the
 * selected + 4: element_slot) and its leader lines vs_scratch.unk8, the area target's marker -4, MAINMENU's background
 * its own (_backgroundOtOffset: element_slot), its command title, rows, hand and tab arrows vs_scratch.unk8 (a row's
 * emit links at unk8 - 4 .. + 12: d.menu_row's own). A classic draw links its packets there too, unless it names another (d.layer). */
static const int kind_slot[PC_HUD_KINDS] = { 0, -16, -16, 8, -20, 0, 0, 0, -12, -4, -4, -12, PC_HUD_LAYER_UNK8, -20, -4,
    PC_HUD_LAYER_ELEMENT, 0, 0, 0, PC_HUD_LAYER_ELEMENT, PC_HUD_LAYER_ELEMENT, PC_HUD_LAYER_UNK8, -4,
    PC_HUD_LAYER_ELEMENT, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8,
    PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, -24, -12, PC_HUD_LAYER_UNK8, -12, -12, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8,
    PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, PC_HUD_LAYER_UNK8, 0, 0, -28, 0 };
static int element_slot; /* PC_HUD_LAYER_ELEMENT: the slot the element just recorded was linked into */
static const int gauge_slots[2] = { -12, -16 };
/* (the classic layer's kinds are the HUD layer's, by number) */
typedef char pc_hud_classic_kinds_agree[PC_CLASSIC_STATUS == PC_HUD_STATUS && PC_CLASSIC_CHAIN == PC_HUD_CHAIN
            && PC_CLASSIC_BUFF == PC_HUD_BUFF && PC_CLASSIC_DAMAGE == PC_HUD_DAMAGE && PC_CLASSIC_GAUGES == PC_HUD_GAUGES
            && PC_CLASSIC_FIGURE == PC_HUD_FIGURE && PC_CLASSIC_STATUS_ICONS == PC_HUD_STATUS_ICONS
            && PC_CLASSIC_BATTLE_MODE == PC_HUD_BATTLE_MODE && PC_CLASSIC_ABILITY_ICONS == PC_HUD_ABILITY_ICONS
            && PC_CLASSIC_ACTOR_WORD == PC_HUD_ACTOR_WORD && PC_CLASSIC_TIMER == PC_HUD_TIMER
            && PC_CLASSIC_EMOTE == PC_HUD_EMOTE && PC_CLASSIC_TEXT_FRAME == PC_HUD_TEXT_FRAME
            && PC_CLASSIC_TEXT_BAND == PC_HUD_TEXT_BAND && PC_CLASSIC_BOX_TEXT == PC_HUD_BOX_TEXT
            && PC_CLASSIC_IMAGE == PC_HUD_IMAGE && PC_CLASSIC_TARGET_ROW == PC_HUD_TARGET_ROW
            && PC_CLASSIC_TARGET_LINE == PC_HUD_TARGET_LINE && PC_CLASSIC_TARGET_MARKER == PC_HUD_TARGET_MARKER
            && PC_CLASSIC_MENU_BACKGROUND == PC_HUD_MENU_BACKGROUND && PC_CLASSIC_MENU_COMMAND == PC_HUD_MENU_COMMAND
            && PC_CLASSIC_MENU_ROW == PC_HUD_MENU_ROW && PC_CLASSIC_MENU_CURSOR == PC_HUD_MENU_CURSOR
            && PC_CLASSIC_MENU_TABS == PC_HUD_MENU_TABS && PC_CLASSIC_OPTION_VALUE == PC_HUD_OPTION_VALUE
            && PC_CLASSIC_ITEMS_CATEGORY == PC_HUD_ITEMS_CATEGORY
            && PC_CLASSIC_ITEMS_STATUS_BUTTON == PC_HUD_ITEMS_STATUS_BUTTON && PC_CLASSIC_EQUIP_STATS == PC_HUD_EQUIP_STATS
            && PC_CLASSIC_DP_PP_BARS == PC_HUD_DP_PP_BARS && PC_CLASSIC_GEM_SLOTS == PC_HUD_GEM_SLOTS
            && PC_CLASSIC_STAT_PAGE == PC_HUD_STAT_PAGE && PC_CLASSIC_OK_MARK == PC_HUD_OK_MARK
            && PC_CLASSIC_STORAGE_BUTTONS == PC_HUD_STORAGE_BUTTONS && PC_CLASSIC_STORAGE_COUNTS == PC_HUD_STORAGE_COUNTS
            && PC_CLASSIC_LOOT_CAPACITIES == PC_HUD_LOOT_CAPACITIES && PC_CLASSIC_DISCARD_COUNT == PC_HUD_DISCARD_COUNT
            && PC_CLASSIC_RENAME_GRID == PC_HUD_RENAME_GRID && PC_CLASSIC_RENAME_BOX == PC_HUD_RENAME_BOX
            && PC_CLASSIC_MENUF == PC_HUD_MENUF && PC_CLASSIC_MENUF_LOAD == PC_HUD_MENUF_LOAD
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
    int slot = layer != PC_HUD_LAYER_KIND ? layer : kind_slot[kind >= 1 && kind < PC_HUD_KINDS ? kind : 1];
    if (slot == PC_HUD_LAYER_ELEMENT)
        slot = element_slot;
    if (PC_HUD_LAYER_IS_UNK8_AT(slot)) /* (vs_scratch.unk8 + bytes) */
        return (int)((char*)psx_get(vs_scratch.unk8) - (char*)psx_get(vs_scratch.unk4)) + (slot + 0x7F00);
    /* Resolve before shadow lookup/sort: aliases coalesce and real OT order is preserved. */
    return slot == PC_HUD_LAYER_UNK8
        ? (int)((char*)psx_get(vs_scratch.unk8) - (char*)psx_get(vs_scratch.unk4)) : slot;
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
/* The text boxes' own ordering table (D_800F51B8: 34 entries, 4 a box, spliced into the frame's by func_800CB83C;
 * drawn from its first entry): a box's frame links into its entries 1 (the shadow) and 2 (the body, the outline,
 * the draw modes). A classic frame drawn in the parity mode goes into a table of its own (box_shadow, never
 * spliced). */
extern u_long* D_800F51B8; /* 5BF94.c */
static u_long box_shadow[34], band_icon_shadow; /* (band_icon_shadow: vs_scratch.unk8's for the band's icon) */
static int box_shadow_id = -1;
/* A menu row's emit links at vs_scratch.unk8 - 4, + 0, + 4, + 8 and + 12 (its glyphs, the scroll arrow and the
 * material, the row icon, the window and its sprites, the tile): in the parity mode d.menu_row runs with unk8 pointed
 * at menu_shadow + 2 (never drawn). */
static u_long menu_shadow[8];
static int menu_shadowed;
extern int pc_hw_anchor_muted __attribute__((weak)), pc_hw_anchor_muted_as __attribute__((weak));
static int text_recording, text_glyph_count, text_box_id; /* the text boxes' glyphs (below, char_packets) */
static void* image_records[3]; /* the images recorded this frame (pc_hud_image, image_packets) */
/* A kind's entries: the frames' 1 and 2, the text's 3 (vs_battle_printVariableWidthFontChar links at the box's
 * entry 3). Then the head of `icon` (vs_scratch.unk8: the band's icon, func_800CD0FC, links there), when it is of
 * this range. */
static int box_entries(int kind, const u_long* table, int id, const u_long* icon, const void* first, const void* end,
    pc_classic_packet* out, int max)
{
    pc_classic_walker walker = pc_classic_guest();
    int n = 0, e, from = kind == PC_HUD_BOX_TEXT ? 3 : 1, to = kind == PC_HUD_BOX_TEXT ? 3 : 2;
    if (id < 0 || id > 7)
        return 0;
    for (e = from; e <= to; ++e)
        n += pc_classic_walk(&walker, (const unsigned*)&table[id * 4 + e], first, end, out + n, max - n);
    if (icon)
        n += pc_classic_walk(&walker, (const unsigned*)icon, first, end, out + n, max - n);
    return n;
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
    box_shadow_id = -1;
    if (&pc_hw_anchor_muted_as)
        pc_hw_anchor_muted_as = -1;
    for (i = 0; i < 34; ++i)
        box_shadow[i] = 0x00FFFFFF;
    for (i = 0; i < 8; ++i)
        menu_shadow[i] = 0x00FFFFFF;
    menu_shadowed = 0;
    classic_packets(cmds, count, anchor, kind, 1);
    shadowing = 0;
    lua_anchor = !pc_hw_anchor_recorded || anchor == PC_HUD_ANCHOR_GAME ? 0
        : pc_hw_anchor_recorded(anchor == PC_HUD_ANCHOR_LEFT ? PC_ANCHOR_LEFT_EDGE
                : anchor == PC_HUD_ANCHOR_RIGHT          ? PC_ANCHOR_RIGHT_EDGE
                                                         : PC_ANCHOR_CENTRE);
    if (&pc_hw_anchor_muted_as && pc_hw_anchor_muted_as >= 0)
        lua_anchor = pc_hw_anchor_muted_as; /* (a game routine anchored its own range, as it does the game's) */
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
    if (box_shadow_id >= 0) { /* (its own anchor: the walker's, as the game's capture) */
        pc_classic_walker box = pc_classic_guest();
        box.anchor = shadow_anchor;
        for (j = kind == PC_HUD_BOX_TEXT ? 3 : 1; j <= (kind == PC_HUD_BOX_TEXT ? 3 : 2); ++j)
            n += pc_classic_walk(&box, (const unsigned*)&box_shadow[box_shadow_id * 4 + j], start,
                psx_get(vs_scratch.unk0), lua + n, PC_CLASSIC_CAPTURE - n);
        if (kind == PC_HUD_TEXT_BAND)
            n += pc_classic_walk(&box, (const unsigned*)&band_icon_shadow, start, psx_get(vs_scratch.unk0), lua + n,
                PC_CLASSIC_CAPTURE - n);
    }
    for (i = 7; menu_shadowed && i >= 0; --i) /* (a menu row's: the higher slot first) */
        n += pc_classic_walk(&walker, (const unsigned*)&menu_shadow[i], start, psx_get(vs_scratch.unk0), lua + n,
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
        int count = 0;
        if (kind == PC_HUD_TEXT_FRAME || kind == PC_HUD_TEXT_BAND || kind == PC_HUD_BOX_TEXT) /* (the text boxes'
                                                                                                * own table) */
            count = box_entries(kind, D_800F51B8, f->box_id, kind == PC_HUD_TEXT_BAND ? psx_get(vs_scratch.unk8) : NULL, first,
                end, game, PC_CLASSIC_CAPTURE);
        else
            count = pc_classic_walk_slots(slots, nslots, kind == PC_HUD_ABILITY_ICONS, enclosing, first, end, game,
                PC_CLASSIC_CAPTURE);
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
        { /* (and by the slots' links: an empty stat bar's 3-word tile in 14 words ends the allocation walk) */
            char* base = psx_get(vs_scratch.unk4);
            int i;
            for (i = 0; i < nslots; ++i)
                pc_classic_suppress_linked((const unsigned*)(base + slots[i]), first, end);
            if (kind == PC_HUD_ABILITY_ICONS)
                pc_classic_suppress_linked((const unsigned*)psx_get(vs_scratch.unk8), first, end);
        }
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

/* Each group excludes pc_weapon_slots_label's own packets/side effects. Nine stable keys. */
void pc_hud_ability_icons(void* record, void* first, void* end)
{
    static pc_hud_fields fields[9];
    const pc_classic_ability_icons* icons = record;
    pc_hud_fields* f = &fields[icons->group];
    *f = (pc_hud_fields) { 0 };
    f->kind = PC_HUD_ABILITY_ICONS;
    f->key = f;
    f->ability_animation = icons->animation;
    f->ability_offset = icons->offset;
    f->ability_group = icons->group;
    f->ability_availability = icons->availability;
    f->anchor = PC_HUD_ANCHOR_CENTRE;
    classic_finish(PC_HUD_ABILITY_ICONS, f, first, end, NULL, 0, 0);
}

/* A word over an actor (2842C.c's label loop): which draw the game chose and its inputs, copied at the call;
 * x, y the place the loop gave it. One element per label record. */
void pc_hud_actor_word(void* record, int x, int y, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -20 };
    const pc_classic_actor_word* word = record;
    if (!word || first == end)
        return; /* nothing drawn (no pieces, off the screen) */
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_ACTOR_WORD;
    f.key = word->label;
    f.age = word->age;
    f.call_x = x, f.call_y = y;
    f.x = (float)x, f.y = (float)y;
    f.word_miss = word->miss;
    f.word_label = word->miss ? 0 : word->index;
    f.word_lowered = !word->miss && word->lowered;
    append(f.text, f.text + sizeof f.text, word->miss ? "MISS" : "");
    if (!word->miss)
        number(append(f.text, f.text + sizeof f.text, "label "), f.text + sizeof f.text, f.word_label);
    f.life = DAMAGE_LIFE;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    classic_finish(PC_HUD_ACTOR_WORD, &f, first, end, slots, 1, 0);
}

/* An image (func_800CCA90): which, its drawer, CLUT word and slot, as the game called the drawer; one element per
 * image. Only the tiled drawer (D_800EC25C[0]: the opening's cards) is the classic layer's: the others (the
 * credits' wipes, ...) stay the game's until a scene the gate plays shows them. */
void pc_hud_image(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    static char keys[3];
    const pc_classic_image* image = record;
    if (!image || first == end || image->image < 0 || image->image > 2 || image->mode != 0)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_IMAGE;
    f.key = &keys[image->image];
    f.image_index = image->image, f.image_mode = image->mode, f.image_clut = image->clut;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "image");
    element_slot = image->slot;
    image_records[image->image] = image->gim;
    classic_finish(PC_HUD_IMAGE, &f, first, end, &image->slot, 1, 0);
}

/* A target row (58578.c func_800C1DC4): its name (copied), place, selected, the plate's fade (before the game steps
 * it) and colour bank, and the slot it linked into. One element per row. */
void pc_hud_target_row(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    const pc_classic_target_row* row = record;
    if (!row || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TARGET_ROW;
    f.key = row->row;
    if (row->text)
        append(f.text, f.text + sizeof f.text, row->text);
    f.row_selected = row->selected != 0, f.row_fade = row->fade, f.row_bank = row->bank;
    f.call_x = row->x, f.call_y = row->y;
    f.x = (float)row->x + 36, f.y = (float)row->y + 4, f.w = 72, f.h = 8; /* (the plate's box) */
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    element_slot = row->slot;
    classic_finish(PC_HUD_TARGET_ROW, &f, first, end, &row->slot, 1, 0);
}

/* A selected row's leader lines (func_800C1DC4): their start, the row's place and their age. One element per row. */
void pc_hud_target_line(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    const pc_classic_target_line* line = record;
    int slot;
    if (!line || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TARGET_LINE;
    f.key = line->row;
    f.call_x = line->x, f.call_y = line->y;
    f.row_x = line->row_x, f.row_y = line->row_y;
    f.age = line->age;
    f.x = (float)line->x, f.y = (float)line->y;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "line");
    slot = (int)((char*)psx_get(vs_scratch.unk8) - (char*)psx_get(vs_scratch.unk4));
    classic_finish(PC_HUD_TARGET_LINE, &f, first, end, &slot, 1, 0);
}

/* The area target's marker (func_800C28AC): its place, lock and brightness step. */
void pc_hud_target_marker(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -4 };
    const pc_classic_target_marker* marker = record;
    if (!marker || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TARGET_MARKER;
    f.key = &f;
    f.call_x = marker->x, f.call_y = marker->y;
    f.marker_lock = marker->lock, f.marker_step = marker->step;
    f.x = (float)marker->x, f.y = (float)marker->y;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "marker");
    classic_finish(PC_HUD_TARGET_MARKER, &f, first, end, slots, 1, 0);
}

/* MAINMENU's shared frame (58EC.c's PC_CLASSIC sites, classic_hook.h): each piece as the game drew it, with what
 * it drew it from. A row is one element per row (0..39); the others one each. The game's packets are walked from
 * its slots: the background's own, vs_scratch.unk8 (a row's: unk8 - 4 .. + 12). */
/* The screens toured (vs_battle_menuState.currentMenu & 0x3F, a bit each; menu_model.c SCREENS): 1 Magic (MENU0),
 * 2 Break Arts (MENU1), 3 Battle Abilities (MENU2), 4 Items (MENU3), 5 Status (MENU4), 6 the Map (MENU5), 7 Data
 * (MENU7), 8 the Option screen (MENU8), 9 Score (MENU9), 10 the Quick Manual (MENUE), 12 Loot (MENUB), 13 Setup
 * (MENUC), 14 Storage (MENUD). Not here: 15 (MENUF: its own kinds,
 * menuf and menuf_load, pc_hud_menuf and the immediate site; it draws none of these; 11 unused). */
#define MENU_TOURED_SCREENS 0x77FEu /* bits 1..10, 12..14 */
/* The screens each of the Items screen's own panels is claimed on (bits as above, by kind from PC_HUD_ITEMS_CATEGORY),
 * each where a tour covers it: 4 Items (MENU3) all six; 5 Status (MENU4), 13 Setup (MENUC) and 14 Storage (MENUD) the
 * C48.c panels (stats, DP / PP bars, table); 12 Loot (MENUB's Organize inventory: items0.h's icons, items1.h's status
 * views, its own copy of the STATUS strip, 260.c) all six; MENUD's own copy of the gem slots (234.c _gripNavigation)
 * and of the category icons (234.c func_80103E24); a misc item's discard amount (items1.h on Items and Loot's Organize inventory, MENUD's copy). The other screens' own: the OK mark on 13 Setup (MENUC _drawOk) and
 * 14 Storage (its top menu), Storage's button strip and counts (14), Loot's capacities strip (12); the rename grid and its
 * highlight boxes on 8 (MENU8's _renderRenameMenu: from Setup's Rename, currentMenu 8). */
#define PANEL_C48 (1u << 4 | 1u << 5 | 1u << 12 | 1u << 13 | 1u << 14)
/* The screens BATTLE's hand (5BF94.c vs_battle_drawCursor, drawn as menu_cursor) is claimed on: 12 Loot (MENUB's Yes / No
 * and row menus), 13 Setup (MENUC's Yes / No), 8 the rename grid's Yes / No (MENU8 _confirmationScreen: the Option
 * screen's own menus draw no BATTLE hand), 7 MENU7's container prompts (_promptYesNo: before Storage, and its warning
 * after; the Data screen's file menu draws its own hand, by DrawPrim), 3 Battle Abilities (MENU2's learned list).
 * Not: the battle command list (the menu closed): no MAINMENU screen. */
#define BATTLE_CURSOR_SCREENS (1u << 3 | 1u << 7 | 1u << 8 | 1u << 12 | 1u << 13)
static const unsigned short PANEL_SCREENS[PC_HUD_KINDS - PC_HUD_ITEMS_CATEGORY] = {
    [PC_HUD_ITEMS_CATEGORY - PC_HUD_ITEMS_CATEGORY] = 1u << 4 | 1u << 12 | 1u << 14, /* (14: MENUD's copy, 234.c) */
    [PC_HUD_ITEMS_STATUS_BUTTON - PC_HUD_ITEMS_CATEGORY] = 1u << 4 | 1u << 12,
    [PC_HUD_EQUIP_STATS - PC_HUD_ITEMS_CATEGORY] = PANEL_C48,
    [PC_HUD_DP_PP_BARS - PC_HUD_ITEMS_CATEGORY] = PANEL_C48,
    [PC_HUD_GEM_SLOTS - PC_HUD_ITEMS_CATEGORY] = 1u << 4 | 1u << 12 | 1u << 14,
    [PC_HUD_STAT_PAGE - PC_HUD_ITEMS_CATEGORY] = PANEL_C48,
    [PC_HUD_OK_MARK - PC_HUD_ITEMS_CATEGORY] = 1u << 13 | 1u << 14,     /* MENUC's _drawOk, MENUD's top menu */
    [PC_HUD_STORAGE_BUTTONS - PC_HUD_ITEMS_CATEGORY] = 1u << 14,
    [PC_HUD_STORAGE_COUNTS - PC_HUD_ITEMS_CATEGORY] = 1u << 14,
    [PC_HUD_LOOT_CAPACITIES - PC_HUD_ITEMS_CATEGORY] = 1u << 12,
    [PC_HUD_DISCARD_COUNT - PC_HUD_ITEMS_CATEGORY] = 1u << 4 | 1u << 12 | 1u << 14, /* items1.h on Items and Loot's
                                                                                    * Organize inventory, MENUD's copy */
    [PC_HUD_RENAME_GRID - PC_HUD_ITEMS_CATEGORY] = 1u << 8, /* MENU8 _renderRenameMenu */
    [PC_HUD_RENAME_BOX - PC_HUD_ITEMS_CATEGORY] = 1u << 8,  /* MENU8 _highlightSelection */
};
static unsigned char menu_row_item[0x40]; /* the row the element just recorded drew: d.menu_row's copy */
void pc_hud_menu(int kind, void* record, void* first, void* end)
{
    static pc_hud_fields rows[40], one[5];
    pc_hud_fields* f;
    int slots[5], nslots = 1, unk8 = (int)((char*)psx_get(vs_scratch.unk8) - (char*)psx_get(vs_scratch.unk4));
    if (!record || first == end)
        return;
    /* Only the screens the gate tours (MENU_TOURED_SCREENS): every other MAINMENU screen keeps the game's own
     * drawing (no element, no claim) until its tour is green (the Quick Manual crashed with Lua's rows). */
    if ((vs_battle_menuState.currentMenu & 0x3F) > 31
        || !(MENU_TOURED_SCREENS & (1u << (vs_battle_menuState.currentMenu & 0x3F))))
        return;
    /* The Items screen's own panels (piece 2): each on the screens whose tour covers it in every state (PANEL_SCREENS);
     * elsewhere the game draws it (items0.h / items1.h are MENUB's too; C48.c's panels are MENU4's, MENUC's, MENUD's). */
    if (kind >= PC_HUD_ITEMS_CATEGORY && kind < PC_HUD_KINDS
        && !(PANEL_SCREENS[kind - PC_HUD_ITEMS_CATEGORY] & (1u << (vs_battle_menuState.currentMenu & 0x3F))))
        return;
    /* BATTLE's hand (vs_battle_drawCursor) as menu_cursor: on the screens whose tours walk its menus only. */
    if (kind == PC_HUD_MENU_CURSOR && ((const pc_classic_menu_cursor*)record)->battle
        && !(BATTLE_CURSOR_SCREENS & (1u << (vs_battle_menuState.currentMenu & 0x3F))))
        return;
    if (kind == PC_HUD_STAT_PAGE) {
        static pc_hud_fields page;
        const pc_classic_stat_page* p = record;
        typedef char pc_hud_stat_page_agrees[sizeof page.stat_page == sizeof *p ? 1 : -1];
        f = &page;
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        memcpy(&f->stat_page, p, sizeof f->stat_page);
        f->x = p->x_offset + 16, f->y = 46, f->w = 160, f->h = 128;
        slots[0] = -12, slots[1] = -16;
        nslots = 2;
    } else if (kind == PC_HUD_EQUIP_STATS || kind == PC_HUD_DP_PP_BARS || kind == PC_HUD_GEM_SLOTS) {
        static pc_hud_fields panels[3];
        f = &panels[kind - PC_HUD_EQUIP_STATS];
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        if (kind == PC_HUD_EQUIP_STATS) {
            const pc_classic_equip_stats* q = record;
            typedef char pc_hud_equip_agrees[sizeof f->equip == sizeof *q ? 1 : -1];
            memcpy(&f->equip, q, sizeof f->equip);
            f->x = (short)f->equip.position, f->y = f->equip.position >> 16, f->w = 72, f->h = 60;
            slots[0] = -24;
        } else if (kind == PC_HUD_DP_PP_BARS) {
            const pc_classic_dp_pp_bars* b = record;
            typedef char pc_hud_bars_agree[sizeof f->bars == sizeof *b ? 1 : -1];
            memcpy(&f->bars, b, sizeof f->bars);
            f->x = 10, f->y = f->bars.step * 8 - 8, f->w = 136, f->h = 16;
            slots[0] = -12;
        } else {
            f->gem_count = ((const pc_classic_gem_slots*)record)->count;
            f->x = 256, f->y = 32, f->w = 56, f->h = 16;
            slots[0] = unk8 + 4, slots[1] = unk8;
            nslots = 2;
        }
    } else if (kind == PC_HUD_ITEMS_CATEGORY || kind == PC_HUD_ITEMS_STATUS_BUTTON) {
        static pc_hud_fields items[2];
        f = &items[kind - PC_HUD_ITEMS_CATEGORY];
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        if (kind == PC_HUD_ITEMS_CATEGORY) {
            const pc_classic_items_category* c = record;
            f->items_step = c->step, f->items_highlight = c->highlight, f->items_selected = c->selected;
            f->items_on_row = c->on_row, f->items_slot = c->slot;
            f->x = 32, f->y = c->step, f->w = 128, f->h = 16;
            slots[0] = c->slot, slots[1] = unk8;
            nslots = 2;
        } else {
            const pc_classic_items_status_button* b = record;
            f->items_step = b->row, f->items_highlight = b->state;
            f->x = 8 - b->row, f->y = 36, f->w = 96, f->h = 16;
            slots[0] = -12, slots[1] = unk8;
            nslots = 2;
        }
    } else if (kind >= PC_HUD_OK_MARK && kind <= PC_HUD_DISCARD_COUNT) { /* Setup's, Storage's and Loot's own */
        static pc_hud_fields own[PC_HUD_DISCARD_COUNT - PC_HUD_OK_MARK + 1];
        f = &own[kind - PC_HUD_OK_MARK];
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        slots[0] = unk8, slots[1] = -12;
        nslots = 2;
        if (kind == PC_HUD_OK_MARK) {
            const pc_classic_ok_mark* o = record;
            typedef char pc_hud_ok_agrees[sizeof f->ok == sizeof *o ? 1 : -1];
            memcpy(&f->ok, o, sizeof f->ok);
            f->x = o->x, f->y = o->y, f->w = 16, f->h = 8;
            slots[0] = -12;
            nslots = 1;
        } else if (kind == PC_HUD_DISCARD_COUNT) {
            const pc_classic_discard_count* q = record;
            typedef char pc_hud_discard_agrees[sizeof f->discard == sizeof *q ? 1 : -1];
            memcpy(&f->discard, q, sizeof f->discard);
            f->x = q->x - 12, f->y = q->y, f->w = 18, f->h = 12;
            nslots = 1;
        } else if (kind == PC_HUD_STORAGE_BUTTONS) {
            const pc_classic_storage_buttons* b = record;
            typedef char pc_hud_storage_buttons_agree[sizeof f->storage_buttons == sizeof *b ? 1 : -1];
            memcpy(&f->storage_buttons, b, sizeof f->storage_buttons);
            f->x = b->x - 8, f->y = 36, f->w = 152, f->h = 28;
        } else if (kind == PC_HUD_STORAGE_COUNTS) {
            const pc_classic_storage_counts* c = record;
            typedef char pc_hud_storage_counts_agree[sizeof f->storage_counts == sizeof *c ? 1 : -1];
            memcpy(&f->storage_counts, c, sizeof f->storage_counts);
            f->x = c->layout ? 32 : c->x, f->y = c->layout ? 88 : 76, f->w = c->layout ? 224 : 136, f->h = 96;
            nslots = 1;
        } else {
            const pc_classic_loot_capacities* l = record;
            typedef char pc_hud_loot_agrees[sizeof f->loot == sizeof *l ? 1 : -1];
            memcpy(&f->loot, l, sizeof f->loot);
            f->x = l->x + 176, f->y = 12, f->w = 136, f->h = 128;
        }
    } else if (kind == PC_HUD_RENAME_GRID) { /* MENU8's rename grid */
        static pc_hud_fields grid;
        const pc_classic_rename_grid* r = record;
        typedef char pc_hud_rename_agrees[sizeof grid.rename == sizeof *r ? 1 : -1];
        f = &grid;
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        memcpy(&f->rename, r, sizeof f->rename);
        f->x = 0, f->y = 30, f->w = 320, f->h = 194;
        slots[0] = -4, slots[1] = -8, slots[2] = unk8, slots[3] = unk8 + 4;
        nslots = 4;
    } else if (kind == PC_HUD_RENAME_BOX) { /* a highlight box on it: the character's (10 wide), the name's pen */
        static pc_hud_fields boxes[2];
        const pc_classic_rename_box* b = record;
        typedef char pc_hud_rename_box_agrees[sizeof boxes[0].rename_box == sizeof *b ? 1 : -1];
        f = &boxes[b->w == 10 ? 0 : 1];
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        memcpy(&f->rename_box, b, sizeof f->rename_box);
        f->x = b->x, f->y = b->y, f->w = b->w, f->h = b->h;
        slots[0] = unk8;
        nslots = 1;
    } else if (kind == PC_HUD_OPTION_VALUE) { /* our Option pages' value column (classic_options.c) */
        static pc_hud_fields values[16];
        const pc_classic_option_value* v = record;
        f = &values[v->row % 16];
        *f = (pc_hud_fields) { 0 };
        f->key = (const void*)(values + v->row % 16);
        f->opt_row = v->row, f->opt_kind = v->kind, f->opt_enabled = v->enabled, f->opt_focused = v->focused;
        f->opt_index = v->index, f->opt_right = v->right, f->opt_lit = v->lit, f->opt_other = v->other;
        f->opt_gap = v->gap, f->opt_bar_colour = v->bar_colour, f->opt_bar_width = v->bar_width, f->opt_fill = v->fill;
        f->opt_link = v->link, f->opt_upper = v->upper, f->opt_window_x = v->window_x;
        f->call_x = v->x, f->call_y = v->y;
        f->x = v->x, f->y = v->y, f->w = v->right - v->x, f->h = 12;
        append(f->opt_value, f->opt_value + sizeof f->opt_value, v->value ? v->value : "");
        append(f->opt_on, f->opt_on + sizeof f->opt_on, v->on ? v->on : "");
        append(f->opt_off, f->opt_off + sizeof f->opt_off, v->off ? v->off : "");
        append(f->opt_label, f->opt_label + sizeof f->opt_label, v->label ? v->label : "");
        slots[0] = unk8 - 4, slots[1] = unk8 + 8;
        nslots = 2;
    } else if (kind == PC_HUD_MENU_ROW) {
        const pc_classic_menu_row* row = record;
        f = &rows[row->id % 40];
        *f = (pc_hud_fields) { 0 };
        f->key = row->row;
        f->menu_id = row->id;
        memcpy(f->menu_item, row->item, sizeof f->menu_item);
        memcpy(menu_row_item, row->item, sizeof menu_row_item);
        {
            const vs_battle_menuItem_t* item = (const vs_battle_menuItem_t*)row->item;
            f->x = item->x, f->y = item->y, f->w = item->w, f->h = 12;
            f->call_x = item->x, f->call_y = item->y;
        }
        slots[0] = unk8 - 4, slots[1] = unk8, slots[2] = unk8 + 4, slots[3] = unk8 + 8, slots[4] = unk8 + 12;
        nslots = 5;
    } else {
        f = &one[kind - PC_HUD_MENU_BACKGROUND];
        *f = (pc_hud_fields) { 0 };
        f->key = f;
        slots[0] = unk8;
        if (kind == PC_HUD_MENU_BACKGROUND) {
            const pc_classic_menu_background* b = record;
            f->menu_fade = b->fade, f->menu_brightness = b->brightness, f->menu_slot = b->slot * 4;
            f->menu_frame = b->frame;
            slots[0] = element_slot = b->slot * 4;
        } else if (kind == PC_HUD_MENU_COMMAND) {
            const pc_classic_menu_command* c = record;
            f->menu_action = c->action, f->call_y = c->y;
        } else if (kind == PC_HUD_MENU_CURSOR) {
            const pc_classic_menu_cursor* c = record;
            f->menu_step = c->step, f->menu_color = c->color;
            f->call_x = (short)c->xy, f->call_y = c->xy >> 16;
        } else {
            const pc_classic_menu_tabs* t = record;
            f->menu_mode = t->mode, f->menu_step = t->step, f->menu_lit = t->lit;
        }
    }
    f->kind = kind;
    f->anchor = PC_HUD_ANCHOR_GAME;
    append(f->text, f->text + sizeof f->text, pc_classic_kind_name(kind));
    classic_finish(kind, f, first, end, slots, nslots, 0);
}

/* MENUF's screens (3B8.c's PC_CLASSIC around _execMenu, currentMenu 15 only; and ENDING's MISSION COMPLETE screen, D4.c's
 * range around the same leaves: screen PC_CLASSIC_MENUF_ENDING): one element a frame, its record the
 * leaves' calls (pc_classic_menuf: copied out of the save state's sections for the Lua side, classic_parity.c). The
 * sprites, draw areas and the prompt's sprite link into vs_scratch.unk4 - 28, the prompt's words into unk8. A frame
 * with more calls than the record holds is the game's. */
void pc_hud_menuf(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    const pc_classic_menuf* m = record;
    int slots[2], unk8 = (int)((char*)psx_get(vs_scratch.unk8) - (char*)psx_get(vs_scratch.unk4));
    if (!m || first == end
        || ((vs_battle_menuState.currentMenu & 0x3F) != 15 && m->screen != PC_CLASSIC_MENUF_ENDING) || m->count < 0
        || m->count > PC_CLASSIC_MENUF_CALLS)
        return;
    memcpy(pc_classic_menuf_calls(), m, sizeof *m);
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_MENUF;
    f.key = &f;
    f.menuf.screen = m->screen, f.menuf.count = m->count;
    memcpy(f.menuf.prompt, m->prompt, sizeof f.menuf.prompt);
    f.x = 0, f.y = 0, f.w = 320, f.h = 240;
    f.anchor = PC_HUD_ANCHOR_GAME;
    append(f.text, f.text + sizeof f.text, "menuf");
    slots[0] = -28, slots[1] = unk8;
    classic_finish(PC_HUD_MENUF, &f, first, end, slots, 2, 0);
}

/* A menu row by the game's own emit on a copy of the row just recorded (x, y, w as the command sets them). */
void pc_mainmenu_render_row(vs_battle_menuItem_t* copy); /* 58EC.c */
static void menu_row_packets(const pc_hud_classic_cmd* c, int shadowed)
{
    vs_battle_menuItem_t item;
    void* slot = psx_get(vs_scratch.unk8);
    memcpy(&item, menu_row_item, sizeof item);
    if (c->a & 1)
        item.x = (short)c->x;
    if (c->a & 2)
        item.y = (short)c->y;
    if (c->a & 4)
        item.w = (u_char)c->w;
    if (shadowed) {
        psx_set(vs_scratch.unk8, &menu_shadow[2]);
        menu_shadowed = 1;
    }
    pc_mainmenu_render_row(&item);
    psx_set(vs_scratch.unk8, slot);
}

/* Our Option pages' value text and gauge by classic_options.c's own draws (the labels' slot: vs_scratch.unk8 + 8,
 * its glyphs at - 4), into the menu shadow for the parity gate. */
int pc_options_text_draw(const char* utf8, int upper, int room, int x, int y, int c); /* classic_options.c */
void pc_options_bar_draw(int colour, int fill, int x, int y);
static void option_packets(const pc_hud_classic_cmd* c, int shadowed)
{
    char text[128];
    void* slot = psx_get(vs_scratch.unk8);
    int n = c->length < (int)sizeof text - 1 ? c->length : (int)sizeof text - 1;
    if (shadowed) {
        psx_set(vs_scratch.unk8, &menu_shadow[2]);
        menu_shadowed = 1;
    }
    if (c->type == PC_HUD_CMD_OPTION_BAR) {
        pc_options_bar_draw(c->a, c->b, c->x, c->y);
    } else {
        memcpy(text, c->text ? c->text : "", (size_t)n);
        text[n] = 0;
        pc_options_text_draw(text, c->a, c->w, c->x, c->y, (int)c->color);
    }
    psx_set(vs_scratch.unk8, slot);
}

int vs_mainMenu_renderIntColor(int value, int xy, int color, u_long* before); /* MAINMENU 58EC.c (d.menu_int) */
/* A button's window as MAINMENU's vs_mainmenu_renderButtonBackground builds it (58EC.c; its own copy: the game's
 * links at vs_scratch.unk4 - 12 always): h lines, each a draw mode (dithered), a shaded line (64, 56, 32 at x to
 * 16, 16, 8 at x + w - i) and a draw mode (semi-transparent, half), into ot. */
static void button_background_packets(u_long* ot, int x, int y, int w, int h)
{
    u_long* p = psx_get(vs_scratch.unk0);
    int i;
    for (i = 0; i < h; ++i) {
        p[0] = vs_getTag(u_long[6], ot[0]);
        p[1] = vs_getTpage(0, 0, 0, 0, ditheringOn);
        p[2] = vs_getRGB0(primLineG2, 64, 56, 32);
        p[3] = ((x & 0xFFFF) | ((y + i) << 0x10));
        p[4] = vs_getRGB888(16, 16, 8);
        p[5] = vs_getXY_2((x + w) - i, y + i);
        p[6] = vs_getTpage(0, 0, clut4Bit, semiTransparencyHalf, ditheringOff);
        ot[0] = ((psx_to_int(u_long, p) << 8) >> 8);
        p += 7;
    }
    psx_set(vs_scratch.unk0, p);
}

/* A text box's text (_printVariableWidthFont): the glyphs it set this frame (recorded as it set them); the box. */
void pc_hud_box_text(void* record, int id, void* first, void* end)
{
    static pc_hud_fields f;
    static char keys[8];
    text_recording = 0;
    if (!record || first == end || id < 0 || id > 7)
        return; /* (nothing set: no glyph yet, spaces only) */
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_BOX_TEXT;
    f.key = &keys[id];
    f.box_id = id;
    f.value = text_glyph_count;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "text");
    text_box_id = id;
    classic_finish(PC_HUD_BOX_TEXT, &f, first, end, NULL, 0, 0);
}

/* A text box's band (func_800CD158): the box as the routine reads it (which edge, how far it has grown, its lines,
 * whether it has settled and its icon), copied after the call (the routine changes nothing of it). */
void pc_hud_text_band(void* record, int id, void* first, void* end)
{
    static pc_hud_fields f;
    static char keys[8];
    const vs_battle_textBox* box = record;
    if (!box || first == end || id < 0 || id > 7)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TEXT_BAND;
    f.key = &keys[id];
    f.box_id = id;
    f.band_top = box->y == 0, f.band_edge = box->unk2A, f.band_lines = box->lineCount;
    f.band_settled = box->state == box->unk22 + 1, f.band_icon = box->unk0.unk0_8;
    f.x = 160, f.y = f.band_top ? f.band_edge * 0.5f : 240 - f.band_edge * 0.5f;
    f.w = 320, f.h = (float)f.band_edge;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "band");
    classic_finish(PC_HUD_TEXT_BAND, &f, first, end, NULL, 0, 0);
}

/* A text box's frame (func_800CD3E4): the box as the routine reads it (its place and size this frame, the
 * shape, the flip, the tail), copied after the call (the routine changes nothing of it). */
void pc_hud_text_frame(void* record, int id, void* first, void* end)
{
    static pc_hud_fields f;
    static char keys[8];
    const vs_battle_textBox* box = record;
    if (!box || first == end || id < 0 || id > 7)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TEXT_FRAME;
    f.key = &keys[id];
    f.box_id = id;
    f.box_x = box->unk24, f.box_y = box->unk26, f.box_w = box->unk28, f.box_h = box->unk2A;
    f.box_shape = box->unk0.unk0_8, f.box_flip = box->unk0.unk0_6, f.box_tail = box->unk0.unk0_12;
    f.x = (float)f.box_x + f.box_w * 0.5f, f.y = (float)f.box_y + f.box_h * 0.5f;
    f.w = (float)f.box_w, f.h = (float)f.box_h;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    append(f.text, f.text + sizeof f.text, "frame");
    classic_finish(PC_HUD_TEXT_FRAME, &f, first, end, NULL, 0, 0);
}

/* What func_80095C18 (2842C.c) decided, as it decides it, for the classic layer; func_8009627C's port reads it
 * too. A host definition: the game's units only declare it. */
pc_classic_emote pc_emote_record;
/* An emote over an actor: what func_80095C18 decided (pc_classic_emote, set as it decided it), the slot it
 * linked into (the balloon and its marks: its depth, unk4 + z words; the off-screen arrow -16; a far actor's
 * sprite 0). One element per actor. */
void pc_hud_emote(void* record, int actor, void* first, void* end)
{
    static pc_hud_fields f;
    static char keys[16];
    const pc_classic_emote* emote = record;
    int slot;
    if (!emote || !emote->draw || first == end || actor < 0 || actor >= 16)
        return;
    if (emote->draw == 3)
        return; /* a far actor's sprite stays the game's: no scene the gate plays shows one (func_8009F858) */
    slot = emote->draw == 1 ? emote->z * 4 : emote->draw == 2 ? -16 : 0;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_EMOTE;
    f.key = &keys[actor];
    f.emote_draw = emote->draw;
    f.emote_kind = emote->kind;
    f.emote_side = emote->side;
    f.emote_z = emote->z;
    f.emote_direction = emote->direction;
    f.emote_frame = emote->frame;
    f.call_x = emote->x, f.call_y = emote->y;
    f.x = (float)emote->x, f.y = (float)emote->y;
    f.value = actor;
    append(f.text, f.text + sizeof f.text, emote->draw == 1 ? "balloon" : emote->draw == 2 ? "arrow" : "far");
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    element_slot = slot;
    classic_finish(PC_HUD_EMOTE, &f, first, end, &slot, 1, 0);
}

/* The time attack's timer: the string _renderTimer composed (its digits the game's: 59:59:99 at most) and
 * the call's place. */
void pc_hud_timer(void* record, int x, int y, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -4 };
    const char* text = record;
    int i;
    if (!text || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_TIMER;
    f.key = &f;
    for (i = 0; i < PC_HUD_TEXT - 1 && text[i]; ++i)
        f.text[i] = text[i];
    f.text[i] = 0;
    if (i >= 8) /* (mm:ss:hh) */
        f.minutes = (text[0] - '0') * 10 + text[1] - '0', f.seconds = (text[3] - '0') * 10 + text[4] - '0',
        f.hundredths = (text[6] - '0') * 10 + text[7] - '0';
    f.call_x = x, f.call_y = y;
    f.x = (float)x, f.y = (float)y;
    f.rgb = 0xFFFFFF;
    f.anchor = PC_HUD_ANCHOR_CENTRE;
    classic_finish(PC_HUD_TIMER, &f, first, end, slots, 1, 0);
}

void pc_hud_battle_mode(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -12 };
    const pc_classic_battle_mode* notice = record;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_BATTLE_MODE;
    f.key = &f;
    f.battle_step = notice->step;
    f.battle_pulse = notice->pulse;
    f.slide = notice->slide;
    f.anchor = PC_HUD_ANCHOR_RIGHT;
    classic_finish(PC_HUD_BATTLE_MODE, &f, first, end, slots, 1, 2);
}

/* A copy of the carousel's already-stepped inputs: no writes or decisions here. */
void pc_hud_status_icons(void* record, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -4, -8 };
    const pc_classic_status_icons* icons = record;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_STATUS_ICONS;
    f.key = &f;
    f.icon_previous = icons->previous;
    f.icon_current = icons->current;
    f.icon_next = icons->next;
    f.icon_count = icons->count;
    f.icon_selected = icons->selected;
    f.icon_offset = icons->offset;
    f.slide = icons->slide;
    f.anchor = PC_HUD_ANCHOR_LEFT;
    classic_finish(PC_HUD_STATUS_ICONS, &f, first, end, slots, 2, 1);
}

/* The figure's visibility and limb-condition decisions stay in the game. */
void pc_hud_figure(void* record, int stance, void* first, void* end)
{
    static pc_hud_fields f;
    static const int slots[] = { -4 };
    vs_battle_actor2* actor = record;
    if (!actor || first == end)
        return;
    f = (pc_hud_fields) { 0 };
    f.kind = PC_HUD_FIGURE;
    f.key = &f;
    f.stance = stance;
    for (int i = 0; i < 5; ++i)
        f.limb_status[i] = vs_battle_getLimbStatus(&actor->limbs[i]);
    f.anchor = PC_HUD_ANCHOR_LEFT;
    classic_finish(PC_HUD_FIGURE, &f, first, end, slots, 1, 1);
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
        f.rise = p->unk12;
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
 *   quad     a textured four-point polygon (POLY_FT4) as the label loop builds its numbers and buffs
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
extern u_long* vs_battle_setSprite(int brightness, int xy, int wh, u_long* ot);
static u_long* sprite(u_long* ot, int x, int y, int w, int h, u_long page, u_long uv_clut, unsigned rgb, float alpha)
{
    /* Allocate and link through the game's sprite routine, not a parallel packet writer. */
    u_long* p = vs_battle_setSprite(semi(alpha) ? 0x180 : 0x80, xy(x, y), xy(w, h), ot);
    p[1] = 0xE1000000 | page;
    p[2] = modulate(rgb) | (p[2] & 0xFF000000);
    p[4] = uv_clut;
    return p + 2;
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

/* A textured quad as func_80094B0C and func_8009695C build theirs (setPolyFT4, setXY4, setUV4, setTPage,
 * setClut, AddPrim): the code 0x2C, raw (setShadeTex 1) and semi-transparent (setSemiTrans) bits, the colour
 * word as given; the texels' u0 + w wrap as the game's bytes do. The pads after the last two texel pairs are
 * left 0 (the game leaves whatever the buffer held: the GPU reads neither, nor a raw quad's colour). */
static void quad_ft4(u_long* ot, const pc_hud_classic_cmd* c)
{
    u_long* p = packet(ot, 9);
    u_long u0 = (u_long)c->c & 255, v0 = (u_long)c->d & 255;
    u_long u1 = (u0 + (u_long)c->font) & 255, v1 = (v0 + (u_long)c->align) & 255;
    int x1 = c->x + c->w, y1 = c->y + c->h;
    p[0] = (c->color & 0xFFFFFF) | (u_long)(0x2C | (c->anchor ? 1 : 0) | (semi(c->alpha) ? 2 : 0)) << 24;
    if (c->fit) { /* its corners and texels one by one (a mirrored balloon, a turned arrow) */
        int i;
        for (i = 0; i < 4; ++i) {
            p[1 + 2 * i] = xy(c->x + c->corner[2 * i], c->y + c->corner[2 * i + 1]);
            p[2 + 2 * i] = (u_long)c->uv[2 * i] | (u_long)c->uv[2 * i + 1] << 8;
        }
        p[2] |= ((u_long)c->b & 0xFFFF) << 16;
        p[4] |= ((u_long)c->a & 0xFFFF) << 16;
        return;
    }
    p[1] = xy(c->x, c->y);
    p[2] = u0 | v0 << 8 | ((u_long)c->b & 0xFFFF) << 16;
    p[3] = xy(x1, c->y);
    p[4] = u1 | v0 << 8 | ((u_long)c->a & 0xFFFF) << 16;
    p[5] = xy(c->x, y1);
    p[6] = u0 | v1 << 8;
    p[7] = xy(x1, y1);
    p[8] = u1 | v1 << 8;
}

/* A target row's plate as func_800C1DC4 builds it: one packet of a black tile (its shadow), a draw mode (dithered),
 * the gradient across (POLY_G4: the left colour, the right) and the draw mode back. */
static void plate_packets(u_long* ot, const pc_hud_classic_cmd* c)
{
    u_long* p = packet(ot, 13);
    p[0] = 0x60000000;
    p[1] = xy(c->a, c->b);
    p[2] = xy(c->c, c->d);
    p[3] = 0xE1000200;
    p[4] = (c->color & 0xFFFFFF) | 0x38000000;
    p[5] = xy(c->x, c->y);
    p[6] = c->color2 & 0xFFFFFF;
    p[7] = xy(c->x + c->w, c->y);
    p[8] = c->color & 0xFFFFFF;
    p[9] = xy(c->x, c->y + c->h);
    p[10] = c->color2 & 0xFFFFFF;
    p[11] = xy(c->x + c->w, c->y + c->h);
    p[12] = 0xE1000000;
}

/* A target row's leader line by the game's own routine (func_800C16FC, the left half's func_800C1A40) as
 * func_800C1DC4 calls it: it links into vs_scratch.unk8, so that is the kind's slot (the shadow's, for the parity
 * gate) for the call and given back after. */
void func_800C16FC(int x0, int x1, int count); /* 58578.c */
void func_800C1A40(int x0, int x1, int count);
static void leader_packets(u_long* ot, const pc_hud_classic_cmd* c)
{
    u_long* slot = psx_get(vs_scratch.unk8);
    psx_set(vs_scratch.unk8, ot);
    (c->b ? func_800C1A40 : func_800C16FC)((int)xy(c->x, c->y), (int)xy(c->c, c->d), c->a);
    psx_set(vs_scratch.unk8, slot);
}

/* A text box's frame by the game's own routine (func_800CD3E4) on a copy of the box: the box's place, size, shape,
 * flip and tail set for the call and given back after it (nothing else of the box is read); its packets link into
 * the box's entries of the text boxes' table, or of box_shadow for the parity gate (the vector bubbles,
 * ui_bubbles.c, told nothing of those). */
void func_800CD3E4(int index); /* 5BF94.c */
extern int pc_bubble_frame_muted __attribute__((weak));
static void frame_packets(const pc_hud_classic_cmd* c, int shadowed)
{
    int id = c->a & 7;
    vs_battle_textBox* box = &vs_battle_textBoxes[id];
    vs_battle_textBox_fields fields = box->unk0;
    short place[4] = { box->unk24, box->unk26, box->unk28, box->unk2A };
    u_long* table = D_800F51B8;
    box->unk24 = (short)c->x, box->unk26 = (short)c->y, box->unk28 = (short)c->w, box->unk2A = (short)c->h;
    box->unk0.unk0_8 = (u_int)c->b & 15, box->unk0.unk0_6 = (u_int)c->c & 3, box->unk0.unk0_12 = (u_int)c->d & 15;
    if (shadowed) {
        D_800F51B8 = box_shadow;
        box_shadow_id = id;
        if (&pc_bubble_frame_muted)
            pc_bubble_frame_muted = 1;
    }
    func_800CD3E4(id);
    D_800F51B8 = table;
    if (&pc_bubble_frame_muted)
        pc_bubble_frame_muted = 0;
    box->unk0 = fields;
    box->unk24 = place[0], box->unk26 = place[1], box->unk28 = place[2], box->unk2A = place[3];
}

/* A text box's band by the game's own routine (func_800CD158) on a copy of the box: which edge it grows from, how
 * far, the lines it holds and its icon set for the call and given back after; its packets link into the box's
 * entry 1 (box_shadow's for the parity gate). */
void func_800CD158(int id); /* 5BF94.c */
static void band_packets(const pc_hud_classic_cmd* c, int shadowed)
{
    int id = c->a & 7;
    vs_battle_textBox* box = &vs_battle_textBoxes[id];
    vs_battle_textBox_fields fields = box->unk0;
    short y = box->y, edge = box->unk2A, state = box->state;
    char lines = box->lineCount;
    u_long* table = D_800F51B8;
    u_long* icon = psx_get(vs_scratch.unk8);
    box->y = c->b ? 0 : 1, box->unk2A = (short)c->c, box->lineCount = (char)c->d;
    box->state = c->font ? box->unk22 + 1 : 0;
    box->unk0.unk0_8 = (u_int)c->align & 15;
    if (shadowed) {
        D_800F51B8 = box_shadow;
        box_shadow_id = id;
        band_icon_shadow = 0x00FFFFFF;
        psx_set(vs_scratch.unk8, &band_icon_shadow); /* (its icon's slot: setSpriteDefault reads it there) */
    }
    func_800CD158(id);
    D_800F51B8 = table;
    psx_set(vs_scratch.unk8, icon);
    box->unk0 = fields;
    box->y = y, box->unk2A = edge, box->state = state, box->lineCount = lines;
}

/* The text boxes' glyphs (_printVariableWidthFont): recorded as the printer sets them, between the site's begin
 * and its hook (5BF94.c pc_classic_text_begin, pc_classic_text_char): the code, the font state the printer reads
 * (its table, colour style, brightness) and the place; a classic draw sets them again by the same printer, in the
 * element's box (text_box_id), its font state set for each and given back after. */
#define BOX_GLYPHS 512
static int text_glyphs[BOX_GLYPHS][6];
extern char _fontTable, D_800F4CB8;
extern int _fontBrightness;
int vs_battle_printVariableWidthFontChar(u_int glyphId, int x, int y, u_long* ot); /* 5BF94.c */
void pc_classic_text_begin(void) { text_recording = 1, text_glyph_count = 0; }
void pc_classic_text_char(int glyph, int x, int y)
{
    int* g;
    if (!text_recording || text_glyph_count >= BOX_GLYPHS)
        return;
    g = text_glyphs[text_glyph_count++];
    g[0] = glyph, g[1] = _fontTable, g[2] = D_800F4CB8, g[3] = x, g[4] = y, g[5] = _fontBrightness;
}
int pc_classic_text_muted(void) { return shadowing; } /* (the parity gate's copies: not ui_text's) */
int pc_hud_box_text_glyphs(const int** out)
{
    *out = &text_glyphs[0][0];
    return text_glyph_count;
}
static void char_packets(const pc_hud_classic_cmd* c, int shadowed)
{
    char table = _fontTable, style = D_800F4CB8;
    int brightness = _fontBrightness;
    if (shadowed)
        box_shadow_id = text_box_id;
    _fontTable = (char)c->b, D_800F4CB8 = (char)c->c, _fontBrightness = c->d;
    vs_battle_printVariableWidthFontChar((u_int)c->a, c->x, c->y, (shadowed ? box_shadow : D_800F51B8) + text_box_id * 4);
    _fontTable = table, D_800F4CB8 = style, _fontBrightness = brightness;
}

/* An image by the game's own drawer, as func_800CCA90 calls it (D_800F51C8 cleared first: the drawers set it);
 * into the shadow, the drawers' own widescreen ranges (pc_hw_anchor_packets) are not recorded. */
extern void (*D_800EC25C[])(void*, int, u_long*); /* 5BF94.c (gim_t* its first) */
extern char D_800F51C8;
static void image_packets(u_long* ot, const pc_hud_classic_cmd* c, int shadowed)
{
    if (c->a < 0 || c->a > 2 || c->b < 0 || c->b > 3 || !image_records[c->a])
        return;
    if (shadowed && &pc_hw_anchor_muted)
        pc_hw_anchor_muted = 1;
    D_800F51C8 = 0;
    D_800EC25C[c->b](image_records[c->a], c->c, ot);
    if (&pc_hw_anchor_muted)
        pc_hw_anchor_muted = 0;
}

/* The commands as packets in the kind's slot (shadow: into that word instead, for the parity gate:
 * nothing reported to ui_text or widescreen), the range anchored exactly (PC_ANCHOR_EXACT) where
 * `anchor` says (PC_HUD_ANCHOR_*; d.anchor's, else the element's). */
extern void pc_vram_transfer(int x, int y, int w, int h, void* data, int write); /* boot_runtime.c */
/* The draw's own VRAM buffer (d.image_store, d.image_restore): the last rectangle read (classic_parity.c's: kept out
 * of this unit's statics, which a save state holds, so a claim's draw leaves the state as the game's does). */
static void image_store(const pc_hud_classic_cmd* c)
{
    int w = c->w, h = c->h, *rect;
    unsigned short* pixels = pc_classic_image_buffer(&rect);
    if (w < 0 || h < 0 || w * h > 1024)
        w = h = 0;
    rect[0] = c->x, rect[1] = c->y, rect[2] = w, rect[3] = h;
    if (w && h)
        pc_vram_transfer(c->x, c->y, w, h, pixels, 0);
}
/* An upload's pixels and rectangle: its own (text) or the buffer's (b 1). */
static const void* image_source(const pc_hud_classic_cmd* c, int* x, int* y, int* w, int* h)
{
    if (c->b == 1) {
        int* rect;
        const void* pixels = pc_classic_image_buffer(&rect);
        *x = rect[0], *y = rect[1], *w = rect[2], *h = rect[3];
        return pixels;
    }
    *x = c->x, *y = c->y, *w = c->w, *h = c->h;
    return c->text;
}
static void emit_command(const pc_hud_classic_cmd* c, u_long* ot, int kind, int shadowed)
{
        (void)kind;
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
        case PC_HUD_CMD_SPRITE: {
            u_long* color = sprite(ot, c->x, c->y, c->w, c->h, (u_long)c->a,
                (u_long)(c->c & 255) | ((u_long)(c->d & 255) << 8)
                | ((u_long)(c->b & 0xFFFF) << 16), c->color, c->alpha);
            if (c->align) /* descriptor.set_page=false: game's setSprite leaves a NOP, inheriting the page */
                color[-1] = 0;
            if (c->font) /* descriptor.bgr: exact GPU modulation, not RGB UI colour */
                *color = (*color & 0xFF000000) | c->color;
            break;
        }
        case PC_HUD_CMD_QUAD:
            quad_ft4(ot, c);
            break;
        case PC_HUD_CMD_MENU_ROW:
            menu_row_packets(c, shadowed);
            break;
        case PC_HUD_CMD_OPTION_TEXT:
        case PC_HUD_CMD_OPTION_BAR:
            option_packets(c, shadowed);
            break;
        case PC_HUD_CMD_BUTTON_BACKGROUND:
            button_background_packets(ot, c->x, c->y, c->w, c->h);
            break;
        case PC_HUD_CMD_PACKET: {
            u_long* p = psx_get(vs_scratch.unk0);
            int words[15] = { c->a, c->b, c->c, c->d };
            int k, n = c->w < 1 ? 1 : c->w > 15 ? 15 : c->w;
            if (n > 4 && c->text)
                memcpy(words + 4, c->text, (size_t)(n - 4) * sizeof words[0]);
            p[0] = ((u_long)n << 24) | (ot[0] & 0xFFFFFF);
            for (k = 0; k < n; ++k)
                p[1 + k] = (u_long)(unsigned)words[k];
            ot[0] = (psx_to_int(u_long, p) << 8) >> 8;
            psx_set(vs_scratch.unk0, p + 1 + n);
            break;
        }
        case PC_HUD_CMD_FONT_TEXT: {
            char table = _fontTable, style = D_800F4CB8;
            int brightness = _fontBrightness, k, x = c->x;
            vs_battle_setFontStyle(c->a);
            for (k = 0; k < c->length; ++k) /* (b: a fixed step, the pen moved by it whatever the glyph's width) */
                x = c->b ? (vs_battle_printVariableWidthFontChar((u_char)c->text[k], x, c->y, ot - 3), x + c->b)
                         : vs_battle_printVariableWidthFontChar((u_char)c->text[k], x, c->y, ot - 3);
            _fontTable = table, D_800F4CB8 = style, _fontBrightness = brightness;
            break;
        }
        case PC_HUD_CMD_MENU_INT:
            vs_mainMenu_renderIntColor(c->a, (int)xy(c->x, c->y), (int)c->color, ot);
            break;
        case PC_HUD_CMD_STAT_BAR:
            vs_battle_renderStatBar(c->a, c->b, ot, (int)xy(c->x, c->y));
            break;
        case PC_HUD_CMD_PLATE:
            plate_packets(ot, c);
            break;
        case PC_HUD_CMD_LEADER:
            leader_packets(ot, c);
            break;
        case PC_HUD_CMD_FRAME:
            frame_packets(c, shadowed);
            break;
        case PC_HUD_CMD_BAND:
            band_packets(c, shadowed);
            break;
        case PC_HUD_CMD_CHAR:
            char_packets(c, shadowed);
            break;
        case PC_HUD_CMD_IMAGE:
            image_packets(ot, c, shadowed);
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
        case PC_HUD_CMD_IMAGE_STORE: /* (in an ordering table's draw: at once, before the table is drawn) */
            image_store(c);
            break;
        case PC_HUD_CMD_IMAGE_LOAD:
            if (!shadowed) {
                int x, y, w, h;
                const void* pixels = image_source(c, &x, &y, &w, &h);
                if (pixels && w > 0 && h > 0)
                    pc_vram_transfer(x, y, w, h, (void*)pixels, 1);
            }
            break;
        case PC_HUD_CMD_TEXT_GLYPH: /* (immediate draws only: immediate_packets) */
            break;
        }
}
static void classic_packets(const pc_hud_classic_cmd* cmds, int n, int anchor, int kind, int shadowed)
{
    void* start = psx_get(vs_scratch.unk0);
    int i;
    restyle = !shadowed;
    if (!shadowed && pc_hud_classic_anchor && pc_hud_classic_anchor() >= 0)
        anchor = pc_hud_classic_anchor();
    for (i = n - 1; i >= 0; --i) {
        const pc_hud_classic_cmd* c = &cmds[i];
        if (c->alpha <= 0.01f && c->type != PC_HUD_CMD_WINDOW && c->type != PC_HUD_CMD_BAR && c->type != PC_HUD_CMD_CURSOR)
            continue;
        emit_command(c, slot_for(kind, c->layer), kind, shadowed);
    }
    restyle = 1;
    if (!shadowed && pc_hw_anchor_packets && anchor != PC_HUD_ANCHOR_GAME) /* placed by data: exactly, no heuristics */
        pc_hw_anchor_packets(start, psx_get(vs_scratch.unk0),
            (anchor == PC_HUD_ANCHOR_LEFT ? PC_ANCHOR_LEFT_EDGE
                    : anchor == PC_HUD_ANCHOR_RIGHT ? PC_ANCHOR_RIGHT_EDGE
                                                    : PC_ANCHOR_CENTRE)
                | PC_ANCHOR_EXACT);
}

/* ---- immediate draws (classic_hook.h PC_CLASSIC_IMMEDIATE: MENU7's file menu) ----
 * A claim's commands made in their order (not last to first: nothing links them into a table), each packet drawn at
 * once as it is made (DrawPrim's way; draw 0, the parity mode: only normalised), the image commands done (a read
 * always; an upload only when drawing) and each entry kept as the recorder keeps the game's (classic_parity.h). The
 * packets go to the draw's arena past the frame's (vs_scratch.unk0), linked to no table, and it is given back after.
 * A d.text_glyph before a packet reports it to ui_text as a printer's glyph (pc_text_glyph: its line layout and the
 * button roles' glyph, as MENU7's printer does). */
extern void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance)
    __attribute__((weak));
extern void pc_draw_packet(void* data, int chain); /* boot_runtime.c */
static int immediate_packets(const pc_hud_classic_cmd* cmds, int n, int kind, int draw, pc_classic_packet* out, int max)
{
    u_long head = 0x00FFFFFF;
    void* start = psx_get(vs_scratch.unk0);
    const pc_hud_classic_cmd* glyph = NULL;
    int count = 0, i;
    restyle = draw;
    psx_set(vs_scratch.unk0, (u_long*)pc_classic_immediate_arena()); /* (classic_parity.c's: not the game's) */
    for (i = 0; i < n; ++i) {
        const pc_hud_classic_cmd* c = &cmds[i];
        u_long *from, *to, *p;
        if (c->type == PC_HUD_CMD_TEXT_GLYPH) {
            if (c->a >= 0)
                glyph = c;
            else if (pc_text_glyph && draw) /* (a space: the pen moved, no packet) */
                pc_text_glyph(NULL, 0, -1, c->x << 16, c->y << 16, (c->x + c->b) << 16, c->b);
            continue;
        }
        if (c->type == PC_HUD_CMD_IMAGE_STORE) {
            image_store(c);
            if (count < max)
                pc_classic_immediate_read_entry(&out[count++], c->x, c->y, c->w, c->h);
            continue;
        }
        if (c->type == PC_HUD_CMD_IMAGE_LOAD) {
            int x, y, w, h;
            const void* pixels = image_source(c, &x, &y, &w, &h);
            if (count < max)
                pc_classic_immediate_upload_entry(&out[count++], x, y, w, h, pixels);
            if (draw && pixels && w > 0 && h > 0)
                pc_vram_transfer(x, y, w, h, (void*)pixels, 1);
            continue;
        }
        if (c->alpha <= 0.01f && c->type != PC_HUD_CMD_WINDOW && c->type != PC_HUD_CMD_BAR && c->type != PC_HUD_CMD_CURSOR)
            continue;
        from = psx_get(vs_scratch.unk0);
        emit_command(c, &head, kind, !draw);
        to = psx_get(vs_scratch.unk0);
        for (p = from; p < to; p += 1 + (p[0] >> 24)) {
            int words = (int)(p[0] >> 24), k;
            if (!words)
                continue;
            if (glyph) { /* (its colour word: the sprite's code) */
                for (k = 1; k <= words && ((p[k] >> 24) & 0xE0) != 0x60; ++k)
                    ;
                if (k <= words && pc_text_glyph)
                    pc_text_glyph((void* const[]) { &p[k] }, 1, glyph->a, glyph->x << 16, glyph->y << 16,
                        (glyph->x + glyph->b) << 16, glyph->b);
                glyph = NULL;
            }
            if (count < max) {
                pc_classic_packet* q = &out[count];
                memset(q, 0, sizeof *q);
                q->words = (unsigned char)(words < PC_CLASSIC_PACKET_WORDS ? words : PC_CLASSIC_PACKET_WORDS - 1);
                q->at = (unsigned short)count;
                memcpy(q->w, p + 1, sizeof q->w[0] * q->words);
                ++count;
            }
            if (draw)
                pc_draw_packet(p, 0);
        }
    }
    psx_set(vs_scratch.unk0, start); /* (given back: nothing links to them) */
    restyle = 1;
    return count;
}

/* The file menu: MENU7's on currentMenu 7 (the Data screen, the save list after Storage, the game over's LOAD), the
 * title's copy (TITLE 22C.c: LOAD, the clear-data save) while its ranges say so (pc_classic_immediate_screen; the
 * title runs with no menu state of BATTLE's to read). MENUF's uploads: on its screens (currentMenu 15). */
static int immediate_title;
void pc_classic_immediate_screen(int title) { immediate_title = title; }
static int immediate_screen_of(int kind)
{
    if (kind == PC_HUD_MENUF_LOAD)
        return !immediate_title && (vs_battle_menuState.currentMenu & 0x3F) == 15;
    return immediate_title || (vs_battle_menuState.currentMenu & 0x3F) == 7;
}
void pc_classic_immediate_begin(int kind)
{
    pc_classic_immediate_open(!pc_classic_parity_on() && immediate_screen_of(kind) && pc_hud_honoured(kind)
        && pc_hud_classic_draw);
    if (kind == PC_HUD_MENUF_LOAD) /* (its pixels: the claim's d.image_load handles, in the parity mode too) */
        pc_classic_immediate_keep();
}
void pc_classic_immediate_site(int kind, void* record)
{
    static pc_hud_fields menu_parts[3], elements[10], menuf_load;
    static pc_classic_packet lua[PC_CLASSIC_CAPTURE];
    const pc_classic_packet* game;
    pc_hud_fields* f;
    int n = pc_classic_immediate_close(&game), amount;
    if (!record || !immediate_screen_of(kind)
        || (kind != PC_HUD_FILE_MENU && kind != PC_HUD_FILE_ELEMENT && kind != PC_HUD_MENUF_LOAD)) {
        pc_classic_immediate_release(1);
        return;
    }
    if (kind == PC_HUD_MENUF_LOAD) { /* MENUF's uploads: none this frame, no element */
        const pc_classic_menuf_load* l = record;
        int uploads = pc_classic_immediate_uploads();
        if (!uploads) {
            pc_classic_immediate_release(1);
            return;
        }
        f = &menuf_load;
        *f = (pc_hud_fields) { 0 };
        f->menuf_load.screen = l->screen, f->menuf_load.phase = l->phase, f->menuf_load.uploads = uploads;
        f->x = 0, f->y = 0, f->w = 320, f->h = 240;
    } else if (kind == PC_HUD_FILE_MENU) {
        const pc_classic_file_menu* m = record;
        typedef char pc_hud_file_menu_agrees[sizeof menu_parts[0].file_menu == sizeof *m ? 1 : -1];
        f = &menu_parts[(unsigned)m->part % 3];
        *f = (pc_hud_fields) { 0 };
        memcpy(&f->file_menu, m, sizeof f->file_menu);
        f->x = 0, f->y = m->part == 0 ? 176 : 0, f->w = 320, f->h = m->part == 0 ? 64 : m->part == 2 ? 176 : 240;
    } else {
        const pc_classic_file_element* e = record;
        typedef char pc_hud_file_element_agrees[sizeof elements[0].file == sizeof *e ? 1 : -1];
        f = &elements[(unsigned)e->id % 10];
        *f = (pc_hud_fields) { 0 };
        memcpy(&f->file, e, sizeof f->file);
        f->x = e->x, f->y = e->y, f->w = e->w, f->h = e->h;
    }
    f->key = f;
    f->kind = kind;
    f->anchor = PC_HUD_ANCHOR_GAME;
    append(f->text, f->text + sizeof f->text, pc_classic_kind_name(kind));
    if (pc_classic_capturing()) {
        pc_classic_capture_game(kind, game, n, pc_vsync_count ? pc_vsync_count() : 0, f->text);
        pc_classic_capture_allocated(kind, n);
    }
    amount = pc_hud_record(f);
    if (pc_classic_parity_on() && !amount) {
        const pc_hud_classic_cmd* cmds;
        const char* why = "no Lua";
        int count = pc_hud_classic_parity_draw ? pc_hud_classic_parity_draw(&cmds, &why) : -1;
        pc_classic_immediate_release(1);
        if (count < 0)
            pc_classic_parity_skip(kind, why);
        else
            pc_classic_parity_compare(kind, lua, immediate_packets(cmds, count, kind, 0, lua, PC_CLASSIC_CAPTURE),
                pc_vsync_count ? pc_vsync_count() : 0);
        return;
    }
    if (amount == 2 && pc_classic_immediate_held()) {
        const pc_hud_classic_cmd* cmds;
        int count = pc_hud_classic_draw ? pc_hud_classic_draw(&cmds) : -1;
        if (count >= 0) {
            pc_classic_immediate_release(0); /* (the game's held back: dropped) */
            immediate_packets(cmds, count, kind, 1, lua, 0); /* (nothing kept: this unit's statics are state) */
            return;
        }
    }
    pc_classic_immediate_release(1); /* the game's own drawing (held back: done now) */
}

/* The classic panels (rood.draw.panel with {classic = true}, the classic look): at the game's frame, just
 * before its ordering table is drawn, each panel's commands become packets in the menus' slot (unk4 - 12) of
 * the table, placed by their anchor (default the left edge: the game's 320 x 240, as the menus). Not a claim:
 * nothing of the game's is suppressed. Only where the table is the scene's (vs_scratch.unk4 below its top),
 * and not in a photo's render. */
extern int pc_photo_rendering __attribute__((weak));
static unsigned panel_first, panel_last; /* the last frame's panel packets (guest addresses), RPC hud stats */
void pc_hud_panel_range(unsigned* first, unsigned* last)
{
    *first = panel_first, *last = panel_last;
}
void pc_hud_panels_classic(void* otag)
{
    const pc_hud_classic_cmd* cmds;
    int cursor = 0, anchor, count;
    unsigned long scene = (unsigned long)psx_get(vs_scratch.unk4), top = (unsigned long)otag;
    void* start = psx_get(vs_scratch.unk0);
    if (!pc_hud_classic_panel || (&pc_photo_rendering && pc_photo_rendering))
        return;
    if (!(scene <= top && top - scene < 0x4000u))
        return;
    while ((count = pc_hud_classic_panel(&cursor, &cmds, &anchor)) >= 0)
        if (count > 0)
            classic_packets(cmds, count, anchor, PC_HUD_STATUS, 0);
    panel_first = panel_last = 0;
    if (psx_get(vs_scratch.unk0) != start) {
        panel_first = (unsigned)psx_addr(start);
        panel_last = (unsigned)psx_addr(psx_get(vs_scratch.unk0));
    }
}
