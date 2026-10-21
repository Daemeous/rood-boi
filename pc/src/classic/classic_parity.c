/* Packet capture and the parity gate (classic_parity.h; plan
 * pc/docs/plans/2026-10-20-classic-ui-in-lua.md §3). */
#include "classic_parity.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ui_text.c: a glyph's code from its sprite (diagnostics only; absent in the probe) */
extern unsigned pc_ui_text_glyph_at(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
    __attribute__((weak));
/* gpu_hw.c: the widescreen anchor of a packet in the ordering table being built */
extern int pc_hw_anchor_of(const void* packet) __attribute__((weak));

static const char* const KIND_NAMES[PC_CLASSIC_KINDS] = { "", "status", "chain", "buff", "damage", "", "", "",
    "gauges", "status_panel" }; /* (hud.h's travel, notice, name_window: not the classic layer's) */
const char* pc_classic_kind_name(int kind)
{
    return kind > 0 && kind < PC_CLASSIC_KINDS ? KIND_NAMES[kind] : "";
}
int pc_classic_kind_of(const char* name)
{
    for (int k = 1; name && *name && k < PC_CLASSIC_KINDS; k++)
        if (!strcmp(name, KIND_NAMES[k]))
            return k;
    return 0;
}

static const unsigned* guest_resolve(unsigned link, void* user)
{
    (void)user;
    link &= 0xFFFFFFu;
    return link == 0xFFFFFFu ? NULL : (const unsigned*)(uintptr_t)(link | 0x80000000u);
}
static int guest_anchor(const unsigned* tag, void* user)
{
    (void)user;
    return pc_hw_anchor_of ? pc_hw_anchor_of(tag) : 0;
}
pc_classic_walker pc_classic_guest(void)
{
    pc_classic_walker w = { guest_resolve, guest_anchor, NULL };
    return w;
}

static void keep(const pc_classic_walker* walker, const unsigned* tag, const void* lo, pc_classic_packet* p)
{
    unsigned n = tag[0] >> 24;
    memset(p, 0, sizeof *p);
    p->words = (unsigned char)n;
    for (unsigned i = 0; i < n && i < PC_CLASSIC_PACKET_WORDS; i++)
        p->w[i] = tag[1 + i];
    p->anchor = (signed char)(walker->anchor ? walker->anchor(tag, walker->user) : 0);
    p->at = (unsigned short)(((const char*)tag - (const char*)lo) / 4);
}

int pc_classic_walk(const pc_classic_walker* walker, const unsigned* head, const void* lo, const void* hi,
    pc_classic_packet* out, int max)
{
    int count = 0, steps = 0;
    if (!walker || !head)
        return 0;
    const unsigned* tag = walker->resolve(head[0], walker->user);
    while (tag && (const void*)tag >= lo && (const void*)tag < hi && steps++ < 4096) {
        unsigned n = tag[0] >> 24;
        if (n >= PC_CLASSIC_PACKET_WORDS || (const void*)(tag + 1 + n) > hi)
            break; /* not a packet: the walk ends */
        if (n && count < max)
            keep(walker, tag, lo, &out[count++]);
        tag = walker->resolve(tag[0], walker->user);
    }
    return count;
}

int pc_classic_scan(const pc_classic_walker* walker, const void* lo, const void* hi, pc_classic_packet* out, int max)
{
    const unsigned* tag = lo;
    int count = 0;
    while ((const void*)(tag + 1) <= hi && count < max) {
        unsigned n = tag[0] >> 24;
        if (n >= PC_CLASSIC_PACKET_WORDS || (const void*)(tag + 1 + n) > hi)
            break;
        if (n)
            keep(walker, tag, lo, &out[count++]);
        tag += 1 + n;
    }
    return count;
}

/* Whether a packet only sets draw modes (every word a GP0 E1-E6 command): the game's sprites are
 * one packet of a draw mode and the sprite, which draws. */
static int mode_only(const unsigned* tag)
{
    unsigned n = tag[0] >> 24;
    for (unsigned i = 1; i <= n; i++)
        if (!((tag[i] >> 24) >= 0xE1 && (tag[i] >> 24) <= 0xE6))
            return 0;
    return n != 0;
}

int pc_classic_suppress(void* lo, void* hi)
{
    unsigned* tag = lo;
    int count = 0;
    while ((void*)(tag + 1) <= hi) {
        unsigned n = tag[0] >> 24;
        if (n >= PC_CLASSIC_PACKET_WORDS || (void*)(tag + 1 + n) > hi)
            break;
        if (n && !mode_only(tag)) {
            tag[0] &= 0x00FFFFFFu;
            ++count;
        }
        tag += 1 + n;
    }
    return count;
}

/* ---- fields ---- */

static const char* const FIELD_NAMES[PC_CLASSIC_FIELDS] = { "count", "length", "code", "colour", "xy", "uv", "clut",
    "tpage", "wh", "mode", "anchor", "word" };
const char* pc_classic_field_name(int field)
{
    return field >= 0 && field < PC_CLASSIC_FIELDS ? FIELD_NAMES[field] : "";
}

/* What each word of a packet is: its field, and for words that carry two
 * (a texel pair with the CLUT or the page) the field of the upper half. */
typedef struct {
    signed char lo, hi; /* hi -1: the whole word is lo */
} word_kind;

/* A packet may hold several GP0 commands (the game's sprites: a draw mode,
 * then the sprite); each word's field follows its command. The main command
 * (the first that draws) goes to *main (its word index), -1: none. */
static int layout_main(const pc_classic_packet* p, word_kind* k, int max, int* main)
{
    int n = 0, i = 0;
    if (main)
        *main = -1;
#define PUT(a, b)                                                                                                      \
    do {                                                                                                               \
        if (n < max)                                                                                                   \
            k[n].lo = (signed char)(a), k[n].hi = (signed char)(b);                                                    \
        ++n;                                                                                                           \
    } while (0)
    while (i < p->words && i < PC_CLASSIC_PACKET_WORDS) {
        unsigned c = p->w[i] >> 24;
        int start = n;
        if (main && *main < 0 && ((c >= 0x20 && c < 0x80) || c == 0x02))
            *main = i;
        if (c >= 0x20 && c < 0x40) { /* polygons */
            int verts = c & 8 ? 4 : 3, tex = c & 4, shade = c & 0x10;
            PUT(PC_CLASSIC_FIELD_COLOUR, PC_CLASSIC_FIELD_CODE);
            for (int v = 0; v < verts; v++) {
                if (shade && v)
                    PUT(PC_CLASSIC_FIELD_COLOUR, -1);
                PUT(PC_CLASSIC_FIELD_XY, -1);
                if (tex)
                    PUT(PC_CLASSIC_FIELD_UV, v == 0 ? PC_CLASSIC_FIELD_CLUT : v == 1 ? PC_CLASSIC_FIELD_TPAGE : -1);
            }
        } else if (c >= 0x40 && c < 0x60) { /* lines; a polyline runs to its terminator */
            int shade = c & 0x10, poly = c & 8, points = 0, colour = 0;
            PUT(PC_CLASSIC_FIELD_COLOUR, PC_CLASSIC_FIELD_CODE);
            for (int j = i + 1; j < p->words; j++) {
                if (poly && points >= 2 && (p->w[j] & 0xF000F000u) == 0x50005000u) {
                    PUT(PC_CLASSIC_FIELD_WORD, -1);
                    break;
                }
                PUT(colour ? PC_CLASSIC_FIELD_COLOUR : PC_CLASSIC_FIELD_XY, -1);
                if (!colour)
                    ++points;
                colour = !colour && shade; /* (Gouraud: a colour before each point after the first) */
                if (!poly && points == 2)
                    break;
            }
        } else if (c >= 0x60 && c < 0x80) { /* rectangles: tiles and sprites */
            PUT(PC_CLASSIC_FIELD_COLOUR, PC_CLASSIC_FIELD_CODE);
            PUT(PC_CLASSIC_FIELD_XY, -1);
            if (c & 4)
                PUT(PC_CLASSIC_FIELD_UV, PC_CLASSIC_FIELD_CLUT);
            if (((c >> 3) & 3) == 0)
                PUT(PC_CLASSIC_FIELD_WH, -1);
        } else if (c >= 0xE1 && c <= 0xE6) {
            PUT(PC_CLASSIC_FIELD_MODE, -1);
        } else if (c == 0x02) {
            PUT(PC_CLASSIC_FIELD_COLOUR, PC_CLASSIC_FIELD_CODE);
            PUT(PC_CLASSIC_FIELD_XY, -1);
            PUT(PC_CLASSIC_FIELD_WH, -1);
        } else {
            while (i + (n - start) < p->words)
                PUT(PC_CLASSIC_FIELD_WORD, -1);
        }
        i += n - start;
    }
#undef PUT
    return n;
}
static int layout(const pc_classic_packet* p, word_kind* k, int max) { return layout_main(p, k, max, NULL); }

static int field_of(const pc_classic_packet* p, int word, unsigned a, unsigned b)
{
    word_kind k[PC_CLASSIC_PACKET_WORDS];
    int n = layout(p, k, PC_CLASSIC_PACKET_WORDS);
    if (word < 0 || word >= n)
        return PC_CLASSIC_FIELD_WORD;
    if (k[word].hi >= 0 && ((a ^ b) & 0xFFFF0000u) && !((a ^ b) & 0xFFFFu))
        return k[word].hi;
    if (k[word].hi == PC_CLASSIC_FIELD_CODE && !((a ^ b) & 0xFFFFFFu))
        return PC_CLASSIC_FIELD_CODE;
    return k[word].lo;
}
int pc_classic_field(const pc_classic_packet* p, int word) { return field_of(p, word, 0, 0); }

int pc_classic_compare(const pc_classic_packet* game, int game_count, const pc_classic_packet* lua, int lua_count,
    pc_classic_diff* diff)
{
    pc_classic_diff d = { -1, 0, -1, 0, 0, 0 };
    int n = game_count < lua_count ? game_count : lua_count;
    for (int i = 0; i < n; i++) {
        const pc_classic_packet *a = &game[i], *b = &lua[i];
        int field = -1, word = -1;
        unsigned x = 0, y = 0;
        if (a->words != b->words) {
            field = PC_CLASSIC_FIELD_LENGTH, x = a->words, y = b->words;
        } else {
            for (int w = 0; w < a->words && w < PC_CLASSIC_PACKET_WORDS; w++)
                if (a->w[w] != b->w[w]) {
                    int f = field_of(a, w, a->w[w], b->w[w]);
                    d.fields |= 1u << f;
                    if (field < 0)
                        field = f, word = w, x = a->w[w], y = b->w[w];
                }
            if (field < 0 && a->anchor != b->anchor)
                field = PC_CLASSIC_FIELD_ANCHOR, x = (unsigned)a->anchor, y = (unsigned)b->anchor;
            else if (a->anchor != b->anchor)
                d.fields |= 1u << PC_CLASSIC_FIELD_ANCHOR;
        }
        if (field < 0)
            continue;
        d.fields |= 1u << field;
        if (d.index < 0)
            d.index = i, d.field = field, d.word = word, d.game = x, d.lua = y;
    }
    if (game_count != lua_count) {
        d.fields |= 1u << PC_CLASSIC_FIELD_COUNT;
        if (d.index < 0)
            d.index = n, d.field = PC_CLASSIC_FIELD_COUNT, d.word = -1, d.game = (unsigned)game_count,
            d.lua = (unsigned)lua_count;
    }
    if (diff)
        *diff = d;
    return d.index >= 0;
}

/* ---- JSON ---- */

typedef struct {
    char* out;
    int size, used;
} sink;
static void put(sink* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(sink* s, const char* fmt, ...)
{
    va_list ap;
    if (s->used >= s->size - 1)
        return;
    va_start(ap, fmt);
    int n = vsnprintf(s->out + s->used, (size_t)(s->size - s->used), fmt, ap);
    va_end(ap);
    s->used = n < 0 ? s->size - 1 : s->used + n >= s->size ? s->size - 1 : s->used + n;
}

static const char* kind_word(unsigned c, int words)
{
    static const char* const polys[8] = { "poly_f3", "poly_ft3", "poly_f4", "poly_ft4", "poly_g3", "poly_gt3",
        "poly_g4", "poly_gt4" };
    if (!words)
        return "nop";
    if (c >= 0x20 && c < 0x40)
        return polys[((c & 0x10) >> 2) | ((c & 8) >> 2) | ((c & 4) >> 2)];
    if (c >= 0x40 && c < 0x60)
        return c & 8 ? "polyline" : "line";
    if (c >= 0x60 && c < 0x80)
        return c & 4 ? "sprite" : "tile";
    switch (c) {
    case 0xE1: return "mode";
    case 0xE2: return "texture_window";
    case 0xE3: case 0xE4: return "area";
    case 0xE5: return "offset";
    case 0xE6: return "mask";
    case 0x02: return "fill";
    }
    return "other";
}

int pc_classic_packets_json(const pc_classic_packet* p, int count, char* out, int size)
{
    sink s = { out, size, 0 };
    unsigned page = 0;
    if (size > 0)
        out[0] = 0;
    put(&s, "[");
    for (int i = 0; i < count; i++) {
        const pc_classic_packet* k = &p[i];
        word_kind f[PC_CLASSIC_PACKET_WORDS];
        int m, n = layout_main(k, f, PC_CLASSIC_PACKET_WORDS, &m);
        unsigned c = !k->words ? 0 : m >= 0 ? k->w[m] >> 24 : k->w[0] >> 24;
        int uv0[2] = { -1, -1 }, uv1[2] = { -1, -1 }, has_uv = 0;
        unsigned clut = 0;
        put(&s, "%s{\"i\":%d,\"at\":%u,\"code\":\"0x%02x\",\"kind\":\"%s\",\"anchor\":%d,\"words\":[", i ? "," : "", i,
            k->at, c, kind_word(c, k->words), k->anchor);
        for (int w = 0; w < k->words; w++)
            put(&s, "%s\"0x%08x\"", w ? "," : "", k->w[w]);
        put(&s, "]");
        for (int w = 0; w < n && w < k->words; w++)
            if (f[w].lo == PC_CLASSIC_FIELD_MODE && (k->w[w] >> 24) == 0xE1 && (m < 0 || w < m))
                page = k->w[w] & 0x1FF;
        put(&s, ",\"xy\":[");
        for (int w = 0, first = 1; w < n && w < k->words; w++)
            if (f[w].lo == PC_CLASSIC_FIELD_XY) {
                int x = (int16_t)(k->w[w] & 0xFFFF), y = (int16_t)(k->w[w] >> 16);
                put(&s, "%s[%d,%d]", first ? "" : ",", x, y);
                first = 0;
            }
        put(&s, "]");
        for (int w = 0; w < n && w < k->words; w++) {
            if (f[w].lo == PC_CLASSIC_FIELD_UV) {
                int u = (int)(k->w[w] & 0xFF), v = (int)(k->w[w] >> 8 & 0xFF);
                if (!has_uv)
                    uv0[0] = uv1[0] = u, uv0[1] = uv1[1] = v;
                uv0[0] = u < uv0[0] ? u : uv0[0], uv0[1] = v < uv0[1] ? v : uv0[1];
                uv1[0] = u > uv1[0] ? u : uv1[0], uv1[1] = v > uv1[1] ? v : uv1[1];
                has_uv = 1;
                if (f[w].hi == PC_CLASSIC_FIELD_CLUT)
                    clut = k->w[w] >> 16;
                else if (f[w].hi == PC_CLASSIC_FIELD_TPAGE)
                    page = k->w[w] >> 16 & 0x1FF;
            } else if (f[w].lo == PC_CLASSIC_FIELD_WH) {
                put(&s, ",\"wh\":[%u,%u]", k->w[w] & 0xFFFF, k->w[w] >> 16);
                if (has_uv)
                    uv1[0] = uv0[0] + (int)(k->w[w] & 0xFFFF) - 1, uv1[1] = uv0[1] + (int)(k->w[w] >> 16) - 1;
            }
        }
        if (c >= 0x60 && c < 0x80 && ((c >> 3) & 3)) { /* fixed-size rectangles */
            int side = ((c >> 3) & 3) == 1 ? 1 : ((c >> 3) & 3) == 2 ? 8 : 16;
            if (has_uv)
                uv1[0] = uv0[0] + side - 1, uv1[1] = uv0[1] + side - 1;
        }
        if (m >= 0) {
            unsigned rgb = k->w[m] & 0xFFFFFF;
            put(&s, ",\"colour\":\"#%02x%02x%02x\",\"semi\":%s", rgb & 0xFF, rgb >> 8 & 0xFF, rgb >> 16 & 0xFF,
                c & 2 ? "true" : "false");
        }
        if (has_uv) {
            put(&s, ",\"uv\":[%d,%d,%d,%d],\"clut\":[%u,%u],\"tpage\":[%u,%u,%u]", uv0[0], uv0[1], uv1[0], uv1[1],
                (clut & 63) * 16, clut >> 6 & 511, (page & 15) * 64, (page >> 4 & 1) * 256, page >> 7 & 3);
            unsigned cp = pc_ui_text_glyph_at ? pc_ui_text_glyph_at(page, clut, uv0[0], uv0[1], uv1[0], uv1[1]) : 0;
            if (cp >= 0x20 && cp < 0x7F && cp != '"' && cp != '\\')
                put(&s, ",\"glyph\":\"%c\"", (char)cp);
            else if (cp)
                put(&s, ",\"glyph\":\"U+%04X\"", cp);
        }
        put(&s, "}");
    }
    put(&s, "]");
    return s.used;
}

/* ---- capture and parity ---- */

typedef struct {
    pc_classic_packet game[PC_CLASSIC_CAPTURE], lua[PC_CLASSIC_CAPTURE];
    int game_count, lua_count, allocated;
    unsigned game_vsync, lua_vsync;
    char text[32];
    unsigned long long frames, matched, mismatched, skipped;
    unsigned long long field_counts[PC_CLASSIC_FIELDS];
    int has_first;
    pc_classic_diff first;
    unsigned first_vsync;
    char first_text[32];
    pc_classic_packet first_game, first_lua; /* the differing packets */
    int first_game_ok, first_lua_ok;
    char skip_why[64];
} kind_state;
static kind_state kinds[PC_CLASSIC_KINDS];
static int parity = -1, capturing;

static int dev_mode(void)
{
    const char* rpc = getenv("ROOD_RPC");
    const char* dev = getenv("ROOD_DEV");
    return (rpc && *rpc) || (dev && !strcmp(dev, "1"));
}
int pc_classic_parity_on(void)
{
    if (parity < 0) {
        const char* v = getenv("ROOD_CLASSIC_PARITY");
        parity = v && !strcmp(v, "1") && dev_mode();
    }
    return parity;
}
void pc_classic_parity_set(int on) { parity = on && dev_mode(); }
int pc_classic_capturing(void) { return capturing || pc_classic_parity_on(); }
void pc_classic_capture_on(void) { capturing = 1; }

static void copy_text(char* out, size_t size, const char* text)
{
    snprintf(out, size, "%s", text ? text : "");
    for (char* c = out; *c; c++)
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20)
            *c = '?';
}

void pc_classic_capture_game(int kind, const pc_classic_packet* p, int count, unsigned vsync, const char* text)
{
    if (kind <= 0 || kind >= PC_CLASSIC_KINDS)
        return;
    kind_state* k = &kinds[kind];
    if (count > PC_CLASSIC_CAPTURE)
        count = PC_CLASSIC_CAPTURE;
    memcpy(k->game, p, sizeof *p * (size_t)count);
    k->game_count = count;
    k->game_vsync = vsync;
    copy_text(k->text, sizeof k->text, text);
}

void pc_classic_capture_allocated(int kind, int count)
{
    if (kind > 0 && kind < PC_CLASSIC_KINDS)
        kinds[kind].allocated = count;
}

void pc_classic_parity_compare(int kind, const pc_classic_packet* lua, int count, unsigned vsync)
{
    if (kind <= 0 || kind >= PC_CLASSIC_KINDS)
        return;
    kind_state* k = &kinds[kind];
    pc_classic_diff d;
    if (count > PC_CLASSIC_CAPTURE)
        count = PC_CLASSIC_CAPTURE;
    memcpy(k->lua, lua, sizeof *lua * (size_t)count);
    k->lua_count = count;
    k->lua_vsync = vsync;
    ++k->frames;
    if (!pc_classic_compare(k->game, k->game_count, lua, count, &d)) {
        ++k->matched;
        return;
    }
    ++k->mismatched;
    for (int f = 0; f < PC_CLASSIC_FIELDS; f++)
        if (d.fields >> f & 1)
            ++k->field_counts[f];
    if (!k->has_first) {
        k->has_first = 1;
        k->first = d;
        k->first_vsync = vsync;
        copy_text(k->first_text, sizeof k->first_text, k->text);
        k->first_game_ok = d.index < k->game_count;
        k->first_lua_ok = d.index < count;
        if (k->first_game_ok)
            k->first_game = k->game[d.index];
        if (k->first_lua_ok)
            k->first_lua = lua[d.index];
    }
}

void pc_classic_parity_skip(int kind, const char* why)
{
    if (kind <= 0 || kind >= PC_CLASSIC_KINDS)
        return;
    ++kinds[kind].skipped;
    copy_text(kinds[kind].skip_why, sizeof kinds[kind].skip_why, why);
}

void pc_classic_parity_reset(void)
{
    for (int i = 0; i < PC_CLASSIC_KINDS; i++) {
        kind_state* k = &kinds[i];
        k->frames = k->matched = k->mismatched = k->skipped = 0;
        memset(k->field_counts, 0, sizeof k->field_counts);
        k->has_first = 0;
        k->skip_why[0] = 0;
        k->lua_count = 0;
    }
}

int pc_classic_parity_json(char* out, int size)
{
    sink s = { out, size, 0 };
    put(&s, "{\"parity\":%s,\"kinds\":{", pc_classic_parity_on() ? "true" : "false");
    for (int i = 1, first = 1; i < PC_CLASSIC_KINDS; i++) {
        const kind_state* k = &kinds[i];
        if (!*KIND_NAMES[i])
            continue;
        put(&s, "%s\"%s\":{\"frames\":%llu,\"matched\":%llu,\"mismatched\":%llu,\"skipped\":%llu,\"fields\":{",
            first ? "" : ",", KIND_NAMES[i], k->frames, k->matched, k->mismatched, k->skipped);
        first = 0;
        for (int f = 0, any = 0; f < PC_CLASSIC_FIELDS; f++)
            if (k->field_counts[f]) {
                put(&s, "%s\"%s\":%llu", any ? "," : "", FIELD_NAMES[f], k->field_counts[f]);
                any = 1;
            }
        put(&s, "},\"skip\":\"%s\",\"captured\":%d,\"vsync\":%u,\"text\":\"%s\"", k->skip_why, k->game_count,
            k->game_vsync, k->text);
        if (k->has_first) {
            put(&s, ",\"first\":{\"vsync\":%u,\"index\":%d,\"field\":\"%s\",\"word\":%d,\"game\":\"0x%08x\","
                     "\"lua\":\"0x%08x\",\"text\":\"%s\",\"packets\":",
                k->first_vsync, k->first.index, FIELD_NAMES[k->first.field], k->first.word, k->first.game,
                k->first.lua, k->first_text);
            char json[2048];
            pc_classic_packet pair[2];
            int n = 0;
            if (k->first_game_ok)
                pair[n++] = k->first_game;
            if (k->first_lua_ok)
                pair[n++] = k->first_lua;
            pc_classic_packets_json(pair, n, json, sizeof json);
            put(&s, "%s,\"sides\":\"%s\"}", json,
                k->first_game_ok && k->first_lua_ok ? "game,lua" : k->first_game_ok ? "game" : "lua");
        }
        put(&s, "}");
    }
    put(&s, "}}");
    return s.used;
}

int pc_classic_packets_rpc(const char* name, char* out, int size)
{
    int kind = pc_classic_kind_of(name);
    if (!kind) {
        snprintf(out, (size_t)size, "unknown classic kind '%s' (status, chain, buff, damage, gauges, status_panel)",
            name ? name : "");
        return -1;
    }
    const kind_state* k = &kinds[kind];
    sink s = { out, size, 0 };
    capturing = 1; /* (from now on: the first call may find none yet) */
    put(&s, "{\"kind\":\"%s\",\"vsync\":%u,\"text\":\"%s\",\"count\":%d,\"allocated\":%d,\"packets\":",
        KIND_NAMES[kind], k->game_vsync, k->text, k->game_count, k->allocated);
    if (s.used < size - 1)
        s.used += pc_classic_packets_json(k->game, k->game_count, out + s.used, size - s.used);
    put(&s, ",\"lua_vsync\":%u,\"lua_count\":%d,\"lua\":", k->lua_vsync, k->lua_count);
    if (s.used < size - 1)
        s.used += pc_classic_packets_json(k->lua, k->lua_count, out + s.used, size - s.used);
    put(&s, "}");
    if (s.used >= size - 1) {
        snprintf(out, (size_t)size, "reply too long");
        return -1;
    }
    return s.used;
}
