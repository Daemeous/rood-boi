/* Modern typography for the game's text (ui_text.h; README "2D / UI").
 *
 * The game draws text as textured 2D primitives from its glyph pages, and
 * gpu_hw.c hands every 2D region to the 2D atlas (ui_upscale.c). Here a
 * region is recognised as a glyph by its page, CLUT and texel rectangle:
 *
 *   dialogue  font page (832, 0), 4-bit, CLUT row 222, 12 x 12 cells,
 *             glyphs 189-377 (talk bubbles' hand-lettered capitals)
 *   menu      the same page, glyphs 0-188 (menus, item names, the
 *             information box; menu rows come as 12 x 1 strips)
 *   info      the field UI page (768, 0), CLUT (864, 223), rows 76-95:
 *             wide outlined capitals (RISK ZERO, COMMAND, NEXT), found
 *             through the game's own tables of its widths and positions
 *   digits    the same page, CLUT (832, 223): the HUD numbers, 6 x 10 and
 *             7 x 12 boxes from row 0; the large ones also as the 8 x 12
 *             quads of the numbers that pop up in battle (damage, heal, MP)
 *
 * and its texels are drawn again at the atlas's resolution from the
 * style's font: a signed distance field per glyph (stb_truetype, made at
 * first use and kept), placed on the original glyph box with the
 * original's baseline and cap height (measured on its H, small capitals on
 * its x) and centred on the original glyph's ink, then mapped through the
 * original glyph's own CLUT ramp (coverage 0..1 -> CLUT index 1..max, or
 * 2..max inside a one-pixel outline of index 1), so colours, brightness,
 * gradients, translucency and the colour key are the game's. Line breaks and
 * timing never change. With ui_text_layout glyph (line is the default) the
 * positions and advances do not either (each glyph in its original cell,
 * descenders below it); with line dialogue and menu text is set string by
 * string with the font's own spacing ("string-level layout" below). Characters a font lacks, and the
 * pictographs (button symbols, arrows, notes), stay original.
 *
 * Words the game draws as pictures (HP, MP, COMMAND, MENU, INFORMATION,
 * BATTLE MODE, ...: ui_labels.h, by page, box and hash) are set in the labels
 * style in the word's original box and colours ("fixed labels" below); the
 * battle's status words (SLOW!, 3 CHAINS) as whole words in the status style
 * ("status words"). Every style field is a setting ("settings" below). */
#define _POSIX_C_SOURCE 200809L
#include "ui_text.h"
#include "ui_labels.h"
#include <math.h>
#include <stddef.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

int pc_ui_text = PC_UI_TEXT_ORIGINAL;
const char* const pc_ui_text_names[2] = { "original", "font" };
unsigned pc_ui_text_generation = 1;

/* Defaults: the fonts picked from pc/tools/font_match.py's rankings (OFL or Apache
 * 2.0, vendored in pc/third_party/fonts; THIRD_PARTY.md) with the placement it fitted.
 * The HUD's digit boxes are narrow (5 texels of ink between outlines) and old-style
 * figures are round: fitted to the original's height the large size (7 x 12) would
 * be condensed to about 0.6 of the font's own width (stretched tall), the small one
 * (6 x 10) to about 0.75. DIGITS_ASPECT keeps both at least this proportion (the
 * large ones a little shorter instead; font_match.py digits). */
#define DIGITS_ASPECT 0.78f
#define STYLE_DEFAULTS                                                                                           \
    {                                                                                                            \
        { "dialogue", "pc/third_party/fonts/PermanentMarker/PermanentMarker-Regular.ttf", 1.02f, 0.12f, -0.02f, \
            0.0f, 1, 0, 1.0f, 0.0f, 0.0f },                                                                      \
        { "menu", "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf", 1.18f, 0.0f, -0.02f, 0.0f, 0, 0, \
            1.0f, 0.0f, 0.0f },                                                                                  \
        { "info", "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf", 1.26f, 0.0f, 0.0f, 0.0f, 0, 1, 1.0f, \
            1.0f, 0.0f },                                                                                        \
        { "digits", "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf", 1.07f, 0.0f, 0.02f, DIGITS_ASPECT, 0, 1, \
            1.0f, 1.0f, 0.0f },                                                                                  \
        { "status", "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf", 1.26f, 0.0f, 0.0f, 0.0f, 0, 1,     \
            1.0f, 1.0f, 0.02f },                                                                                 \
        { "labels", "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf", 1.26f, 0.0f, 0.0f, 0.0f, 0, 1,     \
            1.0f, 1.0f, 0.0f },                                                                                  \
    }
pc_ui_text_style pc_ui_text_styles[PC_UI_TEXT_STYLES] = STYLE_DEFAULTS;
static const pc_ui_text_style defaults[PC_UI_TEXT_STYLES] = STYLE_DEFAULTS; /* (settings save what differs) */

/* The game's character set for glyph indices 0..0xBB of a font table
 * (tools/etc/vsString.py); 0: not drawn from a font (pictographs, unused). */
static const uint16_t charset[189] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', /* 0x00 */
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', /* 0x10 */
    'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', /* 0x20 */
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0x152, 0xC0, /* 0x30 */
    0xC1, 0xC2, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD2, 0xD3, 0xD4, 0xD6, /* 0x40 */
    0xD9, 0xDA, 0xDB, 0xDC, 0xDF, 0x153, 0xE0, 0xE1, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, /* 0x50 */
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0, 0, 0, 0, 0, /* 0x60 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x70 */
    0, 0, 0, 0, 0, 0, 0x201E, 0, 0, 0, 0, 0, 0xB7, 0x2014, 0, 0, /* 0x80: „ ‼ ≠ ≦ ≧ ÷ · — ⋯ space */
    '!', '"', '#', '$', '%', '&', '\'', '(', ')', '=', '@', '[', ']', ';', ':', ',', /* 0x90 */
    '.', '/', '\\', '<', '>', '?', '_', '-', '+', '*', '`', '{', '}', 0, 0, 0, /* 0xA0: ♪ △ □ */
    0, 0, 0, 0, 0, 0, 0, 0, 0, '~', 0, 0, 0 /* 0xB0: ○ × ← → ↑ ↓ Lv. ★ ◼ ~ */
};

/* A game string as UTF-8 (ui_text.h; control bytes as tools/etc/vsString.py
 * decodes them). */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size)
{
    int n = 0, i = 0;
    if (out_size <= 0)
        return 0;
    while (text && i < max_bytes) {
        unsigned c = text[i++], cp = 0;
        if (c == 0xE7)
            break;
        if (c == 0x8F || c == 0xE8) {
            cp = ' ';
        } else if (c == 0xFA) {
            cp = ' ';
            ++i; /* the spacing's operand */
        } else if (c >= 0xEC && c != 0xFC) {
            ++i; /* functions and Japanese tables: one operand byte, nothing drawn here (0xFC has none) */
        } else if (c < sizeof charset / sizeof charset[0]) {
            cp = charset[c];
        }
        if (!cp || (cp == ' ' && (n == 0 || out[n - 1] == ' ')))
            continue; /* not a character (0xEB padding, pictographs), or a repeated blank */
        char u[3];
        int len = 1;
        if (cp < 0x80) {
            u[0] = (char)cp;
        } else if (cp < 0x800) {
            u[0] = (char)(0xC0 | cp >> 6), u[1] = (char)(0x80 | (cp & 0x3F)), len = 2;
        } else {
            u[0] = (char)(0xE0 | cp >> 12), u[1] = (char)(0x80 | (cp >> 6 & 0x3F)), u[2] = (char)(0x80 | (cp & 0x3F)),
            len = 3;
        }
        if (n + len >= out_size)
            break;
        memcpy(out + n, u, (size_t)len);
        n += len;
    }
    while (n > 0 && out[n - 1] == ' ')
        --n;
    out[n] = 0;
    return n;
}

/* ---- distance texels (ui_text.h) ---- */

static uint32_t dist_bits(float d)
{
    long q = lrintf(fminf(fmaxf(d, PC_UI_DIST_FAR), -PC_UI_DIST_FAR - 1.0f / 256.0f) * 256.0f);
    return (uint32_t)q & 0x3fffu;
}
uint32_t pc_ui_dist_texel(float d, int mode, int lo, int hi, int outlined)
{
    return 0x80000000u | (uint32_t)(mode & 3) << 28 | (outlined ? 1u << 27 : 0) | (uint32_t)(hi & 15) << 23
         | (uint32_t)(lo & 15) << 19 | dist_bits(d);
}
uint32_t pc_ui_dist_direct(float d, uint16_t colour)
{
    return 0xc0000000u | (uint32_t)(colour >> 15) << 29 | (uint32_t)(colour & 0x7fff) << 14 | dist_bits(d);
}

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
/* As the shaders: colours mixed in 5-bit steps (blend555, below). */
static uint16_t blend555(uint16_t a, uint16_t b, float t);
uint16_t pc_ui_dist_colour(uint32_t t, float d, const uint16_t* lut, float width, float* coverage)
{
    float body = clamp01(0.5f + d / width), outer = clamp01(0.5f + (d + 1.0f) / width);
    *coverage = body;
    if (!(t >> 31))
        return (uint16_t)t;
    if (t >> 30 & 1) /* direct */
        return (uint16_t)((t >> 14 & 0x7fff) | (t >> 29 & 1) << 15);
    int mode = (int)(t >> 28 & 3), hi = (int)(t >> 23 & 15), lo = (int)(t >> 19 & 15);
    if (mode == PC_UI_DIST_LABEL) {
        if (!(t >> 27 & 1))
            return lut[hi];
        *coverage = outer;
        return blend555(lut[lo], lut[hi], body);
    }
    uint16_t b = lut[lo + (int)floorf(body * (float)(hi - lo) + 0.5f)];
    if (mode == PC_UI_DIST_RAMP)
        return b;
    *coverage = outer;
    return blend555(lut[1], b, body);
}

void pc_ui_text_resolve(const uint16_t* vram, unsigned clut, uint32_t* texels, int count, int factor, float least)
{
    const uint16_t* lut = vram + (clut >> 6 & 511) * 1024 + (clut & 63) * 16;
    for (int i = 0; i < count; i++) {
        float c;
        uint32_t t = texels[i];
        if (!(t >> 31))
            continue;
        uint16_t colour = pc_ui_dist_colour(t, pc_ui_dist_of(t), lut, 1.0f / (float)factor, &c);
        texels[i] = c > (least >= 0 ? least : (t >> 30 & 1) ? 0.249f : 0.02f) ? colour : 0;
    }
}

/* An outlined texel's distance for an outline `stroke` PS1 pixels wide: the
 * shaders draw the outline's outer edge at -1 (the originals' one pixel),
 * so the distance outside the body is mapped to put the stroke's outer edge
 * there, its last half pixel unscaled (the anti-aliased edge stays one
 * output pixel wide); inside, as it is. */
static float stroke_distance(float d, float stroke)
{
    if (d >= 0 || stroke == 1.0f || stroke <= 0)
        return d;
    if (stroke < 1.0f)
        return d / stroke;
    float knee = 0.5f - stroke;
    return d <= knee ? d + stroke - 1.0f : d * 0.5f / (stroke - 0.5f);
}

/* Tunables over the styles (settings; ui_text.h). */
#define WORD_SPACE_DEFAULT 1.5f
#define EDGE_MARGIN_DEFAULT 0.15f
#define DESCENDER_LIFT_DEFAULT 1.0f
float pc_ui_text_word_space = WORD_SPACE_DEFAULT, pc_ui_text_edge_margin = EDGE_MARGIN_DEFAULT;
float pc_ui_text_descender_lift = DESCENDER_LIFT_DEFAULT;
/* Small-capital words (info_words, below): on; the PS1 pixels a word set in
 * the font may run past the game's before it is condensed; the most tracking
 * (PS1 pixels between two letters) that widens a narrower word towards the
 * game's width (the rest is centred). */
#define INFO_WORDS_DEFAULT 1.0f
#define INFO_SLACK_DEFAULT 0.5f
#define INFO_TRACKING_DEFAULT 0.75f
float pc_ui_text_info_words = INFO_WORDS_DEFAULT, pc_ui_text_info_slack = INFO_SLACK_DEFAULT;
float pc_ui_text_info_tracking = INFO_TRACKING_DEFAULT;

/* ---- fonts and glyph distance fields ---- */

#define SDF_EM 64   /* distance field pixels per em (cap height ~45: upsampled smoothly at 8x) */
#define SDF_PAD 16  /* field reaches this many of its pixels outside the outline (> one original pixel) */
#define SDF_SCALE (127.0f / SDF_PAD)

/* What an outlined glyph needs beside its body inside the box it is drawn
 * in: the stroke, its anti-aliased edge (ui_text_edge_margin) and the
 * embolden's growth (PS1 pixels at scale s, PS1 pixels per field pixel). */
static float outline_room(const pc_ui_text_style* st, float s)
{
    float stroke = st->outline ? st->stroke : 0.0f;
    return stroke + pc_ui_text_edge_margin + fmaxf(st->embolden, 0.0f) * SDF_EM * s;
}

typedef struct {
    char path[256];
    unsigned char* data;
    stbtt_fontinfo info;
    float scale; /* font units -> field pixels */
    int ok;
} font_file;
#define MAX_FONTS 64 /* the defaults and any tried through the settings (kept until exit) */
static font_file fonts[MAX_FONTS];
static int font_count;

typedef struct {
    uint32_t key;       /* style << 24 | codepoint (0: empty) */
    unsigned char* sdf; /* NULL: the font lacks it */
    int w, h, xoff, yoff;
    float x0, x1, top, bottom; /* outline box in field pixels (y up from the baseline) */
    float* profile;            /* ui_text_layout line: per field row from profile_y0 up, the ink's left and
                                * right (pairs; left > right: no ink), made at first use */
    int profile_y0, profile_rows;
} glyph_field;
#define GLYPH_SLOTS 2048
static glyph_field glyphs[GLYPH_SLOTS];
static int glyph_count;
static unsigned glyph_generation;
static pc_ui_text_stats stats;

static font_file* font_for(const char* path)
{
    for (int i = 0; i < font_count; i++)
        if (!strcmp(fonts[i].path, path))
            return fonts[i].ok ? &fonts[i] : NULL;
    if (font_count == MAX_FONTS) {
        static int warned;
        if (!warned++)
            fprintf(stderr, "[ui] ui_text: more than %d fonts tried: %s stays original\n", MAX_FONTS, path);
        return NULL;
    }
    font_file* f = &fonts[font_count++];
    memset(f, 0, sizeof *f);
    snprintf(f->path, sizeof f->path, "%s", path);
    FILE* file = fopen(path, "rb");
    long size = 0;
    if (file && !fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET)) {
        f->data = malloc((size_t)size);
        if (f->data && fread(f->data, 1, (size_t)size, file) == (size_t)size) {
            int offset = stbtt_GetFontOffsetForIndex(f->data, 0);
            f->ok = offset >= 0 && stbtt_InitFont(&f->info, f->data, offset);
        }
    }
    if (file)
        fclose(file);
    if (!f->ok) {
        fprintf(stderr, "[ui] ui_text: cannot load font %s: those glyphs stay original\n", path);
        free(f->data);
        f->data = NULL;
        return NULL;
    }
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, SDF_EM);
    ++stats.fonts;
    return f;
}

static void drop_glyphs(void)
{
    for (int i = 0; i < GLYPH_SLOTS; i++)
        if (glyphs[i].sdf) {
            stbtt_FreeSDF(glyphs[i].sdf, NULL);
            free(glyphs[i].profile);
        }
    memset(glyphs, 0, sizeof glyphs);
    glyph_count = 0;
    stats.glyphs = 0;
}

/* The style's field of `codepoint` (NULL: no font, or the font lacks it). */
static const glyph_field* field(int style, unsigned codepoint)
{
    if (glyph_generation != pc_ui_text_generation) {
        drop_glyphs();
        glyph_generation = pc_ui_text_generation;
    }
    uint32_t key = (uint32_t)(style + 1) << 24 | (codepoint & 0xffffff);
    unsigned s = (key * 2654435761u) >> 21 & (GLYPH_SLOTS - 1);
    for (int probe = 0; probe < GLYPH_SLOTS; probe++, s = (s + 1) & (GLYPH_SLOTS - 1)) {
        glyph_field* g = &glyphs[s];
        if (g->key == key)
            return g->sdf ? g : NULL;
        if (g->key)
            continue;
        if (glyph_count >= GLYPH_SLOTS * 3 / 4)
            return NULL;
        g->key = key;
        ++glyph_count;
        font_file* f = font_for(pc_ui_text_styles[style].font);
        int index = f ? stbtt_FindGlyphIndex(&f->info, (int)codepoint) : 0;
        if (!index)
            return NULL;
        int x0, y0, x1, y1;
        if (!stbtt_GetGlyphBox(&f->info, index, &x0, &y0, &x1, &y1))
            return NULL;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        g->sdf = stbtt_GetGlyphSDF(&f->info, f->scale, index, SDF_PAD, 128, SDF_SCALE, &g->w, &g->h, &g->xoff,
            &g->yoff);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        stats.field_ms += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        if (!g->sdf)
            return NULL;
        g->x0 = x0 * f->scale, g->x1 = x1 * f->scale, g->top = y1 * f->scale, g->bottom = y0 * f->scale;
        ++stats.glyphs;
        return g;
    }
    return NULL;
}
/* Signed distance (field pixels, positive inside) at field point (x, y),
 * x right from the origin and y up from the baseline. */
static float distance_at(const glyph_field* g, float x, float y)
{
    float fx = x - g->xoff - 0.5f, fy = -y - g->yoff - 0.5f;
    if (fx < 0 || fy < 0 || fx >= g->w - 1 || fy >= g->h - 1)
        return -1e4f; /* beyond the field: far outside */
    int ix = (int)fx, iy = (int)fy;
    float ax = fx - ix, ay = fy - iy;
    const unsigned char* p = g->sdf + iy * g->w + ix;
    if (!(p[0] | p[1] | p[g->w] | p[g->w + 1]))
        return -1e4f; /* the field's floor: at least SDF_PAD away */
    float top = p[0] + (p[1] - p[0]) * ax, bottom = p[g->w] + (p[g->w + 1] - p[g->w]) * ax;
    return (top + (bottom - top) * ay - 128.0f) / SDF_SCALE;
}

/* ---- icons (button_prompts.c): any font's glyph, finer, kept until exit ---- */

#define ICON_EM 128 /* icons fill 16 x 16 sprites drawn at 8x: 128 pixels */
struct pc_ui_icon {
    glyph_field field;
    int font; /* index in fonts + 1 (0: empty slot) */
};
#define ICON_SLOTS 512
static struct pc_ui_icon icons[ICON_SLOTS];

const pc_ui_icon* pc_ui_icon_glyph(const char* font_path, unsigned codepoint, int* missing)
{
    font_file* f = font_for(font_path);
    *missing = 1;
    if (!f)
        return NULL;
    int font = (int)(f - fonts) + 1;
    unsigned s = ((unsigned)font * 40503u ^ codepoint * 2654435761u) & (ICON_SLOTS - 1);
    for (int probe = 0; probe < ICON_SLOTS; probe++, s = (s + 1) & (ICON_SLOTS - 1)) {
        struct pc_ui_icon* icon = &icons[s];
        if (icon->font == font && icon->field.key == codepoint) {
            *missing = !icon->field.sdf;
            return icon->field.sdf ? icon : NULL;
        }
        if (icon->font)
            continue;
        icon->font = font;
        icon->field.key = codepoint;
        int index = stbtt_FindGlyphIndex(&f->info, (int)codepoint), x0, y0, x1, y1;
        if (!index || !stbtt_GetGlyphBox(&f->info, index, &x0, &y0, &x1, &y1))
            return NULL;
        float scale = stbtt_ScaleForMappingEmToPixels(&f->info, ICON_EM);
        glyph_field* g = &icon->field;
        g->sdf = stbtt_GetGlyphSDF(&f->info, scale, index, SDF_PAD, 128, SDF_SCALE, &g->w, &g->h, &g->xoff, &g->yoff);
        if (!g->sdf)
            return NULL;
        g->x0 = x0 * scale, g->x1 = x1 * scale, g->top = y1 * scale, g->bottom = y0 * scale;
        *missing = 0;
        return icon;
    }
    return NULL;
}
void pc_ui_icon_box(const pc_ui_icon* icon, float box[4])
{
    box[0] = icon->field.x0, box[1] = icon->field.bottom, box[2] = icon->field.x1, box[3] = icon->field.top;
}
float pc_ui_icon_distance(const pc_ui_icon* icon, float x, float y) { return distance_at(&icon->field, x, y); }

/* ---- the game's glyphs ---- */

typedef struct {
    int style;
    unsigned codepoint;
    int box_u, box_v, box_w, box_h; /* the glyph's box in page texels */
    int advance;                    /* the game's advance (0: unknown) */
    int ref_u, ref_v, ref_w, ref_h; /* the style's reference glyph (H, or 0) for cap height and baseline */
    int small_u, small_v;           /* its small capital x (dialogue) */
    int threshold;                  /* CLUT index counted as ink */
    int symbol;                     /* a sign beside numbers: centred on the original's ink (no baseline of its own) */
    int popup;                      /* a battle number popup's glyph (BATTLE.PRG func_80094B0C: damage, heal, MP) */
} glyph_info;

/* Signs beside the HUD's numbers (ui_text_render's digits style): the memory
 * card screens' MAP:34% and TIME 09:41:35 (MENU7.PRG, TITLE.PRG) take their
 * ':' from the digits' page and their '%' (and the other signs a printer
 * draws there) from the info style's small capitals, drawn at once
 * (DrawPrim: pc_gpu_immediate) between the digits; the map screen's own
 * percentages, set in the small capitals throughout, go through the ordering
 * table and stay in the info style. Where the info style's own tables are
 * not loaded (the title), the signs' places in its page (the game's table,
 * BATTLE.PRG) are known here: */
static const struct {
    unsigned char code, u, v, w;
} info_signs[] = {
    { '%', 170, 76, 10 }, { ':', 224, 76, 4 }, { '/', 48, 76, 6 }, { '+', 162, 76, 8 },
    { '-', 154, 76, 7 }, { '.', 242, 86, 4 },
};
extern int pc_gpu_immediate __attribute__((weak)); /* boot_runtime.c: drawn by DrawPrim, not an ordering table */
static int digits_sign(int code, int u, int v, int w, glyph_info* g)
{
    g->style = PC_UI_TEXT_DIGITS;
    g->codepoint = (unsigned)code;
    g->box_u = u, g->box_v = v, g->box_w = w, g->box_h = 10;
    g->advance = w;
    g->ref_u = 0, g->ref_v = 0, g->ref_w = 6, g->ref_h = 10; /* the small digits' 0 */
    g->threshold = 3;
    g->symbol = 1;
    return 1;
}

/* Not from the font: a disc the size and place of the original's (the HUD's
 * HP / MP separator), outlined like the digits. */
#define BULLET 0x2022u

static int page_index(const uint16_t* vram, int page_x, int u, int v)
{
    return (vram[(v & 511) * 1024 + ((page_x + (u >> 2)) & 1023)] >> ((u & 3) * 4)) & 15;
}
/* The game's small-capital tables (BATTLE.PRG, in guest RAM while the
 * field or a battle runs): 4-bit widths and page positions of ' '..'Z'. */
/* Kept from the last time they were valid: menus that load their own
 * overlays (the map) reuse that memory while still drawing these glyphs. */
static unsigned char kept_widths[32], kept_index[0x3c];
static int kept;
static const unsigned char* small_widths(void) { return kept_widths; }
static const unsigned char* small_index(void) { return kept_index; }
static int small_tables_valid(void)
{
    const unsigned char *w = (const unsigned char*)(uintptr_t)0x800EB7B4u,
                        *x = (const unsigned char*)(uintptr_t)0x800EB7D4u;
    int last = -1;
    for (int c = 'A' - 0x20; c <= 'Z' - 0x20; c++) {
        int width = (w[c >> 1] >> ((c << 2) & 4)) & 15;
        int pos = (x[c] & 0xfe) | (x[c] & 1) << 8;
        if (width < 3 || pos <= last)
            return kept;
        last = pos;
    }
    memcpy(kept_widths, w, sizeof kept_widths);
    memcpy(kept_index, x, sizeof kept_index);
    return kept = 1;
}
static int small_find(int code, int* u, int* v, int* w)
{
    int c = code - 0x20;
    if (c < 0 || c >= 0x3b || c == 10)
        return 0;
    *w = (small_widths()[c >> 1] >> ((c << 2) & 4)) & 15;
    *u = small_index()[c] & 0xfe;
    *v = (small_index()[c] & 1) * 10 + 0x4c;
    return *w > 0;
}

static int identify(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1, glyph_info* g)
{
    if ((tpage >> 7 & 3) != 0 || (tpage >> 4 & 1) != 0)
        return 0; /* 4-bit pages at y 0 */
    int page = (int)(tpage & 15), cx = (int)(clut & 63) * 16, cy = (int)(clut >> 6 & 511);
    memset(g, 0, sizeof *g);
    if (page == 13 && cy == 222 && cx >= 832 && cx < 960) {
        int col = u0 / 12, row = v0 / 12;
        if (u1 / 12 != col || v1 / 12 != row || col >= 21 || row >= 18)
            return 0;
        int index = row * 21 + col, table = index / 189;
        unsigned cp = charset[index % 189];
        if (!cp)
            return 0;
        g->style = table ? PC_UI_TEXT_DIALOGUE : PC_UI_TEXT_MENU;
        g->codepoint = cp;
        g->box_u = col * 12, g->box_v = row * 12, g->box_w = 12, g->box_h = 12;
        /* The game's advances (vs_battle_characterWidths, BATTLE.PRG). */
        int advance = ((const unsigned char*)(uintptr_t)0x800EB810u)[index];
        g->advance = advance >= 3 && advance <= 12 ? advance : 0;
        int h = table * 189 + 0x11, x = table * 189 + 0x3b; /* H, x */
        g->ref_u = h % 21 * 12, g->ref_v = h / 21 * 12, g->ref_w = 12, g->ref_h = 12;
        g->small_u = x % 21 * 12, g->small_v = x / 21 * 12;
        g->threshold = 4; /* coverage 0.2 of 15 */
        return 1;
    }
    if (page == 12 && cy == 223 && cx == 864 && (v0 == 76 || v0 == 86) && v1 == v0 + 9) {
        int immediate = &pc_gpu_immediate && pc_gpu_immediate;
        for (size_t i = 0; i < sizeof info_signs / sizeof info_signs[0] && immediate; i++)
            if (info_signs[i].u == u0 && info_signs[i].v == v0 && u1 == u0 + info_signs[i].w - 1)
                return digits_sign(info_signs[i].code, u0, v0, info_signs[i].w, g);
        if (!small_tables_valid())
            return 0;
        /* A stat change popping up in battle (BATTLE.PRG func_80094E18:
         * STR -12) draws its sign as an 8 x 10 quad: the '-' (7 wide) with
         * the column after it. */
        int stat_sign = u0 == 154 && v0 == 76 && u1 == u0 + 7;
        for (int code = 0x21; code <= 0x5a; code++) {
            int u, v, w;
            if (small_find(code, &u, &v, &w) && u == u0 && v == v0
                && (u1 == u0 + w - 1 || (stat_sign && code == '-'))) {
                g->style = PC_UI_TEXT_INFO;
                g->codepoint = (unsigned)code;
                g->box_u = u, g->box_v = v, g->box_w = w, g->box_h = 10;
                g->advance = w;
                if (!small_find('H', &g->ref_u, &g->ref_v, &g->ref_w))
                    return 0;
                g->ref_h = 10;
                g->threshold = 4;
                g->popup = stat_sign && code == '-';
                return 1;
            }
        }
        return 0;
    }
    if (page == 12 && cy == 223 && (cx == 912 || cx == 928) && (u0 == 216 || u0 == 224 || u0 == 232)
        && (v0 == 200 || v0 == 208 || v0 == 216) && u1 == u0 + 6 && v1 == v0 + 6) {
        /* An item's material letter beside its icon (MAINMENU.PRG's
         * _renderItemMaterial: 7 x 7, material - 1 down columns of three
         * from (216, 200)): Wood, Leather, Bronze, Iron, Hagane, Silver,
         * Damascus, outlined in CLUT index 1 as the info style. */
        int id = (u0 - 216) / 8 * 3 + (v0 - 200) / 8;
        if (id > 6)
            return 0;
        g->style = PC_UI_TEXT_INFO;
        g->codepoint = (unsigned char)"WLBIHSD"[id];
        g->box_u = g->ref_u = u0, g->box_v = g->ref_v = v0, g->box_w = g->ref_w = 7, g->box_h = g->ref_h = 7;
        g->advance = 7;
        g->threshold = 2;
        g->symbol = 1;
        return 1;
    }
    if (page == 12 && cy == 223 && cx == 832 && u0 == 248 && v0 == 8 && u1 == 250 && v1 == 15) {
        /* The digits' colon (3 x 8: the memory card screens' times). */
        digits_sign(':', 248, 8, 3, g);
        g->box_h = 8;
        return 1;
    }
    if (page == 12 && cy == 223 && cx == 832 && u0 == 32 && u1 == 36 && v1 == 15 && (v0 == 11 || v0 == 9)) {
        /* The dot between current and maximum HP / MP (_renderDigit's
         * font 2: 5 x 5 at (32, 11); also drawn from row 9). */
        g->style = PC_UI_TEXT_DIGITS;
        g->codepoint = BULLET;
        g->box_u = 32, g->box_v = v0, g->box_w = 5, g->box_h = 16 - v0;
        g->advance = 5;
        g->ref_u = 0, g->ref_v = 0, g->ref_w = 6, g->ref_h = 10;
        g->threshold = 3;
        return 1;
    }
    if (page == 12 && cy == 223 && cx == 832 && v0 == 0) {
        int digit = -1, w = 0, h = 0, ref = 0;
        if (u0 <= 60 && u0 % 6 == 0 && u1 == u0 + 5 && v1 == 9)
            digit = u0 / 6, w = 6, h = 10, ref = 0; /* 10: the small digits' slash */
        else if (u0 >= 64 && u0 < 144 && (u0 - 64) % 8 == 0 && (u1 == u0 + 6 || u1 == u0 + 7) && v1 == 11)
            digit = (u0 - 64) / 8, w = 7, h = 12, ref = 64;
        if (digit < 0)
            return 0;
        g->style = PC_UI_TEXT_DIGITS;
        g->codepoint = digit == 10 ? '/' : '0' + (unsigned)digit;
        g->box_u = u0, g->box_v = 0, g->box_w = w, g->box_h = h;
        g->advance = w;
        g->ref_u = ref, g->ref_v = 0, g->ref_w = w, g->ref_h = h;
        g->threshold = 3; /* coverage 0.2 of 9 */
        /* The numbers that pop up over Ashley and enemies (damage, heal, MP:
         * BATTLE.PRG func_80094B0C) draw the large digits as 8 x 12 quads
         * (the column after the glyph too); the HUD's sprites are 7 wide.
         * The same glyph, drawn into the wider region. */
        g->popup = ref == 64 && u1 == u0 + 7;
        return 1;
    }
    return 0;
}

/* Ink box of a glyph box (texels with index >= threshold): x0, y0, x1, y1
 * relative to the box, exclusive ends; 0 when empty. */
static int ink(const uint16_t* vram, int page_x, int u, int v, int w, int h, int threshold, int box[4])
{
    box[0] = w, box[1] = h, box[2] = box[3] = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (page_index(vram, page_x, u + x, v + y) >= threshold) {
                box[0] = x < box[0] ? x : box[0], box[1] = y < box[1] ? y : box[1];
                box[2] = x + 1 > box[2] ? x + 1 : box[2], box[3] = y + 1 > box[3] ? y + 1 : box[3];
            }
    return box[2] > box[0];
}

static unsigned upper(unsigned cp)
{
    if (cp >= 'a' && cp <= 'z')
        return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
        return cp - 32;
    if (cp == 0x153)
        return 0x152;
    return cp;
}

/* ---- fixed labels (text baked into the UI's images) ---- */

/* A 2D region is (part of) one or more known label words (ui_labels.h)
 * when it lies on their page, every word it touches still holds the same
 * texels (hash) and all the ink it covers belongs to those words. The
 * words are then drawn from the info style's font, fitted into the
 * original word's box: its cap height and baseline, its width (condensed
 * only where the font's word is wider), in the original's colours (the
 * outline's CLUT colour and the body's, row by row) with the info style's
 * one-pixel outline. Pictographs, icons and text in other scripts are not
 * labels and stay original. */
#define MAX_REGION_LABELS 8
static uint64_t labels_drawn[(LABEL_COUNT + 63) / 64]; /* which labels were drawn from the font (RPC) */
static char signs_drawn[32]; /* which signs (glyph_info.symbol) were drawn from the font (RPC) */
/* Battle number popups' glyphs drawn from the font (RPC): which characters,
 * and how many primitives (each frame's triangles) asked for one. */
static char popups_drawn[32];
static long popup_glyphs;
void pc_ui_text_forget_signs(void)
{
    memset(signs_drawn, 0, sizeof signs_drawn);
    memset(popups_drawn, 0, sizeof popups_drawn);
    popup_glyphs = 0;
}

static int texel4(const uint16_t* vram, int page_x, int page_y, int u, int v)
{
    return (vram[((page_y + v) & 511) * 1024 + ((page_x + (u >> 2)) & 1023)] >> ((u & 3) * 4)) & 15;
}

unsigned pc_ui_text_glyph_at(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
{
    glyph_info g;
    return identify(tpage, clut, u0, v0, u1, v1, &g) ? g.codepoint : 0;
}

/* The label word most of a texel region shows (the HUD layer names a quad's image:
 * hud_bridge.c); NULL when it covers none. */
const char* pc_ui_text_label_at(int page_x, int page_y, int u0, int v0, int u1, int v1)
{
    const char* best = NULL;
    int most = 0;
    for (int i = 0; i < LABEL_COUNT; i++) {
        const pc_ui_label* l = &labels[i];
        if (l->page_x != page_x || l->page_y != page_y)
            continue;
        int w = (u1 < l->u1 ? u1 : l->u1) - (u0 > l->u0 ? u0 : l->u0) + 1;
        int h = (v1 < l->v1 ? v1 : l->v1) - (v0 > l->v0 ? v0 : l->v0) + 1;
        if (w > 0 && h > 0 && w * h > most)
            most = w * h, best = l->text;
    }
    return best;
}

static uint32_t label_hash(const uint16_t* vram, const pc_ui_label* l)
{
    uint32_t h = 2166136261u;
    for (int v = l->v0; v <= l->v1; v++)
        for (int u = l->u0; u <= l->u1; u++)
            h = (h ^ (uint32_t)texel4(vram, l->page_x, l->page_y, u, v)) * 16777619u;
    return h;
}

static int find_labels(const uint16_t* vram, unsigned tpage, int u0, int v0, int u1, int v1,
    int found[MAX_REGION_LABELS])
{
    if ((tpage >> 7 & 3) != 0)
        return 0; /* 4-bit pages only */
    int page_x = (int)(tpage & 15) * 64, page_y = (int)(tpage >> 4 & 1) * 256, n = 0;
    for (int i = 0; i < LABEL_COUNT; i++) {
        const pc_ui_label* l = &labels[i];
        if (l->page_x != page_x || l->page_y != page_y || l->u0 > u1 || l->u1 < u0 || l->v0 > v1 || l->v1 < v0)
            continue;
        if (n == MAX_REGION_LABELS || label_hash(vram, l) != l->hash)
            return 0;
        found[n++] = i;
    }
    if (!n)
        return 0;
    /* Nothing else in the region: all its ink is the words'. */
    for (int v = v0; v <= v1; v++)
        for (int u = u0; u <= u1; u++) {
            if (!texel4(vram, page_x, page_y, u, v))
                continue;
            int inside = 0;
            for (int k = 0; k < n && !inside; k++) {
                const pc_ui_label* l = &labels[found[k]];
                inside = u >= l->u0 && u <= l->u1 && v >= l->v0 && v <= l->v1;
            }
            if (!inside)
                return 0;
        }
    return n;
}

#define LABEL_CHARS 32
typedef struct {
    const pc_ui_label* l;
    const glyph_field* glyph[LABEL_CHARS];
    float pen[LABEL_CHARS]; /* field pixels from the first glyph's origin */
    int count;
    float s, sx, cx_t, cx_s, base;
    float sd;               /* the scale below the baseline (descenders squeezed into the box) */
    int disc;               /* a dot (LABEL_DOT): a disc, not letters */
    float disc_x, disc_y, disc_r;
    uint16_t outline;       /* CLUT colour of the original's outline (0: none) */
    uint16_t body;          /* CLUT colour of the original's lettering */
    int outline_index, body_index; /* their CLUT indices */
} label_layout;

static uint16_t blend555(uint16_t a, uint16_t b, float t)
{
    int r = (int)lrintf((a & 31) + ((b & 31) - (a & 31)) * t);
    int g = (int)lrintf((a >> 5 & 31) + ((b >> 5 & 31) - (a >> 5 & 31)) * t);
    int bl = (int)lrintf((a >> 10 & 31) + ((b >> 10 & 31) - (a >> 10 & 31)) * t);
    uint16_t c = (uint16_t)(r | g << 5 | bl << 10 | (b & 0x8000));
    return c ? c : 0x8000; /* never the colour key */
}

static int luma555(uint16_t c) { return (c & 31) * 299 + (c >> 5 & 31) * 587 + (c >> 10 & 31) * 114; }

/* Ink classes of the original words: indices 1-3 are the dark outline
 * (and its shading) of every label CLUT, 4 and up the lettering. */
#define LABEL_BODY 4
/* A label whose text is this (U+2022) is a dot, not a word (the L2
 * shortcut cross's connectors beside its labels): drawn as a disc the size
 * and place of the original dot's lettering, outlined like the labels. */
#define LABEL_DOT "\xe2\x80\xa2"

/* `region` (u0, v0, u1, v1): the texels the game draws the word in; with the
 * label's own box it bounds where the word's ink (and its outline) may go. */
static int label_layout_make(const uint16_t* vram, unsigned clut, const pc_ui_label* l, const int region[4],
    label_layout* o)
{
    const pc_ui_text_style* st = &pc_ui_text_styles[PC_UI_TEXT_LABELS];
    font_file* f = font_for(st->font);
    const uint16_t* lut = vram + (clut >> 6 & 511) * 1024 + (clut & 63) * 16;
    memset(o, 0, sizeof *o);
    o->l = l;
    if (!f || l->v1 - l->v0 >= 32)
        return 0;
    /* The original: body box, cap top, baseline (the commonest bottom of
     * the body's columns: descenders are the exception), colours. */
    int bx0 = 256, bx1 = -1, top = 256, outline_count[4] = { 0 }, bottoms[32] = { 0 };
    for (int u = l->u0; u <= l->u1; u++) {
        int bottom = -1;
        for (int v = l->v0; v <= l->v1; v++) {
            int i = texel4(vram, l->page_x, l->page_y, u, v);
            if (i >= LABEL_BODY) {
                bx0 = u < bx0 ? u : bx0, bx1 = u > bx1 ? u : bx1, top = v < top ? v : top;
                bottom = v;
            } else if (i)
                ++outline_count[i];
        }
        if (bottom >= 0)
            ++bottoms[bottom - l->v0];
    }
    if (bx1 < 0)
        return 0;
    int best = 0;
    for (int k = 1; k < 32; k++)
        best = bottoms[k] > bottoms[best] ? k : best;
    o->base = (float)(l->v0 + best + 1);
    int dark = outline_count[1] >= outline_count[2] ? 1 : 2;
    dark = outline_count[3] > outline_count[dark] ? 3 : dark;
    o->outline = outline_count[dark] ? lut[dark] : 0;
    o->outline_index = dark;
    /* The lettering's colour, not its bevel, shading or banding (a font's
     * letters do not fall on the original's rows): the brightest colour the
     * word uses at least a third as often as its commonest. */
    int counts[16] = { 0 }, mode = 0;
    for (int v = l->v0; v <= l->v1; v++)
        for (int u = l->u0; u <= l->u1; u++) {
            int i = texel4(vram, l->page_x, l->page_y, u, v);
            if (i >= LABEL_BODY)
                ++counts[i];
        }
    for (int i = LABEL_BODY; i < 16; i++)
        mode = counts[i] > counts[mode] ? i : mode;
    int pick = mode;
    for (int i = LABEL_BODY; i < 16 && mode; i++)
        if (counts[i] * 3 >= counts[mode] && luma555(lut[i]) > luma555(lut[pick]))
            pick = i;
    if (!pick || !lut[pick])
        return 0;
    o->body = lut[pick];
    o->body_index = pick;
    if (!strcmp(l->text, LABEL_DOT)) {
        int by1 = top;
        for (int v = l->v0; v <= l->v1; v++)
            for (int u = l->u0; u <= l->u1; u++)
                if (texel4(vram, l->page_x, l->page_y, u, v) >= LABEL_BODY)
                    by1 = v > by1 ? v : by1;
        o->disc = 1;
        o->disc_x = (bx0 + bx1 + 1) * 0.5f, o->disc_y = (top + by1 + 1) * 0.5f;
        o->disc_r = fmaxf((float)(bx1 + 1 - bx0), (float)(by1 + 1 - top)) * 0.5f;
        return 1;
    }
    /* The word in the font: pen positions with the font's advances, kerning
     * and the style's tracking, its ink extent, height above the baseline
     * and depth below it. */
    float pen = 0, x0 = 1e9f, x1 = -1e9f, height = 0, depth = 0;
    int previous = 0;
    for (const char* c = l->text; *c; c++) {
        int index = stbtt_FindGlyphIndex(&f->info, (unsigned char)*c);
        if (previous && index)
            pen += stbtt_GetGlyphKernAdvance(&f->info, previous, index) * f->scale + st->tracking * SDF_EM;
        if (*c != ' ') {
            const glyph_field* g = field(PC_UI_TEXT_LABELS, (unsigned char)*c);
            if (!g || o->count == LABEL_CHARS)
                return 0;
            o->glyph[o->count] = g;
            o->pen[o->count++] = pen;
            x0 = fminf(x0, pen + g->x0), x1 = fmaxf(x1, pen + g->x1), height = fmaxf(height, g->top);
            depth = fmaxf(depth, -g->bottom);
        }
        int advance, bearing;
        stbtt_GetGlyphHMetrics(&f->info, index, &advance, &bearing);
        pen += advance * f->scale;
        previous = index;
    }
    if (!o->count || height <= 0 || x1 <= x0)
        return 0;
    /* Its height is the original's (cap top to baseline, or the tallest
     * letter's top) times the style's size, grown about its middle, its
     * width the font's own; condensed where the word, its outline and that
     * outline's anti-aliased edge would leave the room the original word has
     * (its box within the texels drawn: whatever the font, no ink is cut at
     * the box's edges), smaller where it would leave the box's rows. */
    float cap = o->base - (float)top, mid = o->base - cap * 0.5f;
    float s0 = cap / height, margin = pc_ui_text_edge_margin;
    float size = st->size > 0.1f ? st->size : 0.1f;
    float lo_u = 0, hi_u = 0, hi_v = 0;
    /* The field's distances are scaled by the smaller of the two scales, so
     * the outline is wider along the larger one (sx / s across a stretched
     * word, s / sx down a condensed one): each pass fits the word with the
     * ratios the previous one left (they only shrink: the last fit holds). */
    o->s = s0 * size, o->sx = o->s * st->stretch;
    for (int pass = 0; pass < 3; pass++) {
        float edge = outline_room(st, fmaxf(o->s, s0)) - margin;
        float down = pass && o->sx < o->s ? o->s / o->sx : 1.0f, across = o->sx > o->s ? o->sx / o->s : 1.0f;
        float lo_v = (float)(l->v0 > region[1] ? l->v0 : region[1]) + edge * down + margin;
        hi_v = (float)((l->v1 < region[3] ? l->v1 : region[3]) + 1) - edge * down - margin;
        float half_room = fminf(mid - lo_v, hi_v - mid), fit = size;
        if (cap * fit * 0.5f > half_room)
            fit = fmaxf(half_room * 2.0f / cap, 0.1f);
        o->s = fminf(o->s, s0 * fit);
        o->sx = fminf(o->sx, o->s * st->stretch);
        lo_u = (float)(l->u0 > region[0] ? l->u0 : region[0]) + edge * across + margin;
        hi_u = (float)((l->u1 < region[2] ? l->u1 : region[2]) + 1) - edge * across - margin;
        float room = fmaxf(hi_u - lo_u, 1.0f);
        if ((x1 - x0) * o->sx > room)
            o->sx = room / (x1 - x0);
    }
    o->base = mid + cap * (o->s / s0) * 0.5f;
    o->sd = o->s;
    if (depth * o->s > hi_v - o->base)
        o->sd = hi_v > o->base ? (hi_v - o->base) / depth : 0.0f;
    float half = (x1 - x0) * o->sx * 0.5f;
    o->cx_t = fminf(fmaxf((bx0 + bx1 + 1) * 0.5f, lo_u + half), hi_u - half);
    o->cx_s = (x0 + x1) * 0.5f;
    return 1;
}

static int render_labels(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, uint32_t* out)
{
    int found[MAX_REGION_LABELS];
    int n = find_labels(vram, tpage, u0, v0, u1, v1, found);
    static label_layout layouts[MAX_REGION_LABELS];
    int region[4] = { u0, v0, u1, v1 };
    for (int k = 0; k < n; k++)
        if (!label_layout_make(vram, clut, &labels[found[k]], region, &layouts[k]))
            return 0;
    if (n <= 0)
        return 0;
    const pc_ui_text_style* st = &pc_ui_text_styles[PC_UI_TEXT_LABELS];
    float bold = st->embolden * SDF_EM, inv = 1.0f / factor, stroke = st->outline ? st->stroke : 0.0f;
    int w = (u1 - u0 + 1) * factor, rows = (v1 - v0 + 1) * factor;
    for (int y = 0; y < rows; y++) {
        float yo = v0 + (y + 0.5f) * inv;
        for (int x = 0; x < w; x++) {
            float xo = u0 + (x + 0.5f) * inv;
            uint32_t texel = pc_ui_dist_texel(PC_UI_DIST_FAR, PC_UI_DIST_LABEL, 0, 0, 0);
            float best = -1e9f;
            for (int k = 0; k < n; k++) {
                const label_layout* o = &layouts[k];
                const pc_ui_label* l = o->l;
                if (xo < l->u0 || xo >= l->u1 + 1 || yo < l->v0 || yo >= l->v1 + 1)
                    continue;
                float d = -1e9f;
                if (o->disc)
                    d = o->disc_r - hypotf(xo - o->disc_x, yo - o->disc_y); /* original pixels */
                else {
                    float up = o->base - yo, ks = up >= 0 ? o->s : o->sd;
                    if (ks <= 0)
                        continue;
                    float fx = o->cx_s + (xo - o->cx_t) / o->sx, fy = up / ks;
                    for (int i = 0; i < o->count; i++) {
                        const glyph_field* g = o->glyph[i];
                        float gx = fx - o->pen[i];
                        if (gx < g->x0 - SDF_PAD || gx > g->x1 + SDF_PAD)
                            continue;
                        d = fmaxf(d, distance_at(g, gx, fy));
                    }
                    d = (d + bold) * fminf(o->s, o->sx); /* original pixels (the squeeze below moves rows, not the edge) */
                }
                if (d <= best)
                    continue;
                best = d;
                texel = pc_ui_dist_texel(stroke_distance(d, stroke), PC_UI_DIST_LABEL, o->outline_index,
                    o->body_index, o->outline != 0 && stroke > 0);
            }
            out[y * w + x] = texel;
        }
    }
    for (int k = 0; k < n; k++)
        labels_drawn[found[k] >> 6] |= 1ull << (found[k] & 63);
    return 1;
}

int pc_ui_text_classify(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    uint64_t* key)
{
    glyph_info g;
    static int debug = -1;
    if (debug < 0)
        debug = getenv("ROOD_UI_DEBUG") ? atoi(getenv("ROOD_UI_DEBUG")) : 0;
    if (pc_ui_text != PC_UI_TEXT_FONT || !vram)
        return 0;
    if (!identify(tpage, clut, u0, v0, u1, v1, &g)) {
        int found[MAX_REGION_LABELS];
        int n = find_labels(vram, tpage, u0, v0, u1, v1, found);
        if (n > 0 && field(PC_UI_TEXT_LABELS, 'H')) {
            if (debug > 2) /* ROOD_UI_DEBUG=3: labels drawn from the font */
                fprintf(stderr, "[ui] label %s%s: page %x clut %d,%d uv %d,%d..%d,%d\n", labels[found[0]].text,
                    n > 1 ? " ..." : "", tpage, (int)(clut & 63) * 16, (int)(clut >> 6 & 511), u0, v0, u1, v1);
            *key = (uint64_t)pc_ui_text_generation << 40 ^ (uint64_t)15 << 36 ^ (uint64_t)(found[0] + 1) << 8
                 ^ (uint64_t)n;
            return 1;
        }
        if (debug > 3) /* ROOD_UI_DEBUG=4: 2D regions that are not glyphs */
            fprintf(stderr, "[ui] not a glyph: page %x clut %d,%d uv %d,%d..%d,%d\n", tpage, (int)(clut & 63) * 16,
                (int)(clut >> 6 & 511), u0, v0, u1, v1);
        return 0;
    }
    const pc_ui_text_style* st = &pc_ui_text_styles[g.style];
    unsigned cp = st->small_caps ? upper(g.codepoint) : g.codepoint;
    if (cp != BULLET && !field(g.style, cp))
        return 0;
    if (g.symbol && !strchr(signs_drawn, (int)g.codepoint) && strlen(signs_drawn) < sizeof signs_drawn - 1) {
        signs_drawn[strlen(signs_drawn)] = (char)g.codepoint;
        if (debug > 2) /* ROOD_UI_DEBUG=3: each sign the first time */
            fprintf(stderr, "[ui] sign %c (%s): page %x clut %d,%d uv %d,%d..%d,%d\n", (char)g.codepoint, st->name,
                tpage, (int)(clut & 63) * 16, (int)(clut >> 6 & 511), u0, v0, u1, v1);
    }
    if (g.popup) {
        ++popup_glyphs;
        if (!strchr(popups_drawn, (int)g.codepoint) && strlen(popups_drawn) < sizeof popups_drawn - 1)
            popups_drawn[strlen(popups_drawn)] = (char)g.codepoint;
    }
    *key = (uint64_t)pc_ui_text_generation << 40 ^ (uint64_t)g.style << 36 ^ (uint64_t)g.codepoint << 8;
    return 1;
}

/* ---- battle popups at the UI text scale ----
 *
 * The UI text scale (1: the original's size) is read here only
 * (pc_ui_text_scale): a future setting sets it, the RPC ui_text {scale}
 * does for tests. With ui_text font and a scale other than 1, what pops up
 * over Ashley and enemies in battle is drawn that much larger about its
 * middle, each piece one after another in the ordering table:
 *
 *   numbers   (damage, heal, MP: BATTLE.PRG func_80094B0C) one 8 x 12 quad
 *             per digit, 6 pixels apart, left to right; a stat change
 *             (func_80094E18: STR -12) then its sign and its word, left of
 *             the digits;
 *   MISS      (func_8009506C) its letters from the info style's page, right
 *             to left, squashed and stretched vertically as it comes and goes.
 *
 * Where a popup's first piece is drawn, the pieces that follow it in the
 * ordering table give its extent: its middle across, and down the middle of
 * its last digit (the least of the bounce) or of its letters. Everything else
 * (where, when, colours, motion, brightness) stays the game's. */
static float text_scale = 1.0f;
float pc_ui_text_scale(void) { return text_scale; }
void pc_ui_text_set_scale(float scale) { text_scale = scale >= 0.25f && scale <= 4.0f ? scale : 1.0f; }

enum { PIECE_NONE, PIECE_DIGIT, PIECE_SIGN, PIECE_WORD, PIECE_MISS };
/* What a packet (its tag word) draws of a popup, and its rectangle (x0, y0,
 * x1, y1: exclusive ends, the packet's pixels). */
static int popup_piece(const uint32_t* tag, int r[4])
{
    if (!tag || (tag[0] >> 24) != 9 || ((tag[1] >> 24) & 0xfc) != 0x2c)
        return PIECE_NONE; /* a textured four-point polygon, flat: POLY_FT4 */
    unsigned clut = tag[3] >> 16, page = tag[5] >> 16 & 0x19f;
    int u0 = (int)(tag[3] & 255), v0 = (int)(tag[3] >> 8 & 255), u1 = (int)(tag[9] & 255), v1 = (int)(tag[9] >> 8 & 255);
    r[0] = (int16_t)(tag[2] & 0xffff), r[1] = (int16_t)(tag[2] >> 16);
    r[2] = (int16_t)(tag[8] & 0xffff), r[3] = (int16_t)(tag[8] >> 16);
    if ((int16_t)(tag[4] & 0xffff) != r[2] || (int16_t)(tag[4] >> 16) != r[1] || (int16_t)(tag[6] & 0xffff) != r[0]
        || (int16_t)(tag[6] >> 16) != r[3] || r[2] <= r[0] || r[3] <= r[1])
        return PIECE_NONE; /* an upright rectangle */
    const unsigned digits = 223u << 6 | 832u >> 4, info = 223u << 6 | 864u >> 4;
    if (page == 12 && clut == digits && u0 >= 64 && u0 < 144 && (u0 - 64) % 8 == 0 && v0 == 0 && u1 == u0 + 8
        && v1 == 12 && r[2] - r[0] == 8 && r[3] - r[1] == 12)
        return PIECE_DIGIT;
    if (page == 12 && clut == info && (u0 == 154 || u0 == 162) && v0 == 76 && u1 == u0 + 8 && v1 == 86)
        return PIECE_SIGN;
    if (page == 0x17 && (clut == (220u << 6 | 976u >> 4) || clut == (220u << 6 | 960u >> 4)) && v1 == v0 + 12)
        return PIECE_WORD;
    if (page == 12 && clut == info && v0 == 86 && v1 == 96
        && ((u0 == 100 && u1 == 111) || (u0 == 70 && u1 == 74) || (u0 == 160 && u1 == 169)))
        return PIECE_MISS;
    return PIECE_NONE;
}

/* The popup digits the newest ordering table drew (RPC ui_text popups:
 * tests), whichever ui_text: the digit and the packet's top-left. */
#define MAX_POPUP_DIGITS 16
static struct {
    char digit;
    int x, y;
} popup_digits[MAX_POPUP_DIGITS];
static int popup_digit_count;
static unsigned popup_digit_vsync;
extern unsigned pc_vsync_count(void) __attribute__((weak));

#define MAX_POPUP_PIECES 8
int pc_ui_text_popup_place(const uint32_t* tag, const uint32_t* (*next)(const uint32_t*), float xy[8])
{
    static const uint32_t* members[MAX_POPUP_PIECES];
    static int member_count;
    static float anchor[2];
    int r[4], kind = popup_piece(tag, r);
    if (kind == PIECE_NONE)
        return 0;
    if (kind == PIECE_DIGIT) {
        unsigned now = pc_vsync_count ? pc_vsync_count() : 0;
        if (now != popup_digit_vsync)
            popup_digit_count = 0, popup_digit_vsync = now;
        if (popup_digit_count < MAX_POPUP_DIGITS) {
            popup_digits[popup_digit_count].digit = (char)('0' + ((tag[3] & 255) - 64) / 8);
            popup_digits[popup_digit_count].x = r[0], popup_digits[popup_digit_count].y = r[1];
            ++popup_digit_count;
        }
    }
    if (text_scale == 1.0f || pc_ui_text != PC_UI_TEXT_FONT)
        return 0;
    int known = 0;
    for (int i = 0; i < member_count && !known; i++)
        known = members[i] == tag;
    if (!known) {
        if (kind != PIECE_DIGIT && kind != PIECE_MISS)
            return 0; /* (a sign or word drawn alone) */
        /* A popup's first piece: it and the pieces after it. */
        int box[4] = { r[0], r[1], r[2], r[3] }, last[4] = { r[0], r[1], r[2], r[3] }, q[4];
        member_count = 0;
        members[member_count++] = tag;
        for (const uint32_t* p = next ? next(tag) : NULL; p && member_count < MAX_POPUP_PIECES; p = next(p)) {
            int k = popup_piece(p, q);
            if (kind == PIECE_DIGIT && k == PIECE_DIGIT && q[0] == last[0] + 6 && abs(q[1] - last[1]) <= 16)
                memcpy(last, q, sizeof last); /* the next digit */
            else if (kind == PIECE_DIGIT && (k == PIECE_SIGN || k == PIECE_WORD) && q[2] <= box[0] + 1
                     && q[2] >= box[0] - 8)
                ; /* a stat change's sign, then its word, left of what came before */
            else if (kind == PIECE_MISS && k == PIECE_MISS && q[2] == box[0] && q[1] == box[1])
                ; /* the next letter, left of the previous */
            else
                break;
            members[member_count++] = p;
            box[0] = q[0] < box[0] ? q[0] : box[0], box[2] = q[2] > box[2] ? q[2] : box[2];
        }
        anchor[0] = (box[0] + box[2]) * 0.5f;
        anchor[1] = (last[1] + last[3]) * 0.5f;
    }
    for (int i = 0; i < 4; i++) {
        xy[i * 2] = anchor[0] + (xy[i * 2] - anchor[0]) * text_scale;
        xy[i * 2 + 1] = anchor[1] + (xy[i * 2 + 1] - anchor[1]) * text_scale;
    }
    return 1;
}

/* The scale of one digit size (s: fitted to the original's height): all
 * ten digits share it, and the widest still fits `room` condensed to no
 * less than `aspect` of the font's proportions; else they are smaller. */
static float digit_scale(int style, float s, float aspect, float room)
{
    float widest = 0;
    for (unsigned d = '0'; d <= '9'; d++) {
        const glyph_field* q = field(style, d);
        if (q && q->x1 - q->x0 > widest)
            widest = q->x1 - q->x0;
    }
    if (widest > 0 && widest * s * aspect > room)
        s = room / (widest * aspect);
    return s;
}

int pc_ui_text_render(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, uint32_t* out)
{
    glyph_info g;
    if (!identify(tpage, clut, u0, v0, u1, v1, &g)) {
        if (render_labels(vram, tpage, clut, u0, v0, u1, v1, factor, out)) {
            ++stats.regions;
            ++stats.labels;
            return 1;
        }
        ++stats.failed;
        return 0;
    }
    const pc_ui_text_style* st = &pc_ui_text_styles[g.style];
    int page_x = (int)(tpage & 15) * 64;
    int small = st->small_caps && upper(g.codepoint) != g.codepoint;
    int bullet = g.codepoint == BULLET;
    const glyph_field* f = bullet ? field(g.style, '0') : field(g.style, small ? upper(g.codepoint) : g.codepoint);
    const glyph_field* h = field(g.style, 'H');
    int box[4], ref[4], xbox[4];
    if (!f || !h || !ink(vram, page_x, g.box_u, g.box_v, g.box_w, g.box_h, g.threshold, box)
        || !ink(vram, page_x, g.ref_u, g.ref_v, g.ref_w, g.ref_h, g.threshold, ref)) {
        ++stats.failed;
        return 0;
    }
    /* Vertical metrics: the original's cap height and baseline (small
     * capitals: its x's). */
    float base = (float)ref[3], height = (float)(ref[3] - ref[1]);
    float cap = g.style == PC_UI_TEXT_DIGITS ? (field(g.style, '0') ? field(g.style, '0')->top : h->top) : h->top;
    if (small && ink(vram, page_x, g.small_u, g.small_v, 12, 12, g.threshold, xbox))
        base = (float)xbox[3], height = (float)(xbox[3] - xbox[1]);
    if (height < 1 || cap <= 0) {
        ++stats.failed;
        return 0;
    }
    float s = height / cap; /* original pixels per field pixel */
    /* Outlined: the ink, its outline and that outline's anti-aliased edge
     * stay inside the box (a popup's: the wider quad it is drawn in), so
     * nothing is cut at its edges. */
    float box_w = g.popup && u0 == g.box_u && u1 - u0 + 1 > g.box_w ? (float)(u1 - u0 + 1) : (float)g.box_w;
    float edge = st->outline ? outline_room(st, s) : 0.0f;
    float room = st->outline ? fmaxf(box_w - 2.0f * edge, 1.0f)
                             : fmaxf((float)(box[2] - box[0]), (float)g.advance) + 0.5f;
    if (g.style == PC_UI_TEXT_DIGITS && !bullet && st->aspect > 0) /* (a sign: the small digits' scale) */
        s = digit_scale(g.style, s, st->aspect, g.symbol ? g.ref_w + 0.5f : room);
    float sx = s * st->stretch;
    if ((f->x1 - f->x0) * sx > room)
        sx = room / (f->x1 - f->x0);
    /* Descenders: the PS1 faces have none, so the box has little room below
     * the baseline (the large HUD digits: 2 rows, less the outline's room).
     * The digits' figures are old style (3 4 5 7 9 descend): squeezed alone
     * their descenders flatten (a 5's bowl) while the rest keeps its scale.
     * So the digits of one size first rise together, by what their deepest
     * descender lacks, into the room above their tallest figure
     * (ui_text_descender_lift of it: 1 all it takes, 0 none); a descender
     * still too deep is squeezed. */
    float below = g.box_h - base - (st->outline ? edge : 0.1f), sd = s;
    if (g.style == PC_UI_TEXT_DIGITS && !bullet && !g.symbol && pc_ui_text_descender_lift > 0) {
        float deepest = 0, tallest = 0;
        for (unsigned d = '0'; d <= '9'; d++) {
            const glyph_field* q = field(g.style, d);
            if (q)
                deepest = fmaxf(deepest, -q->bottom), tallest = fmaxf(tallest, q->top);
        }
        float lacks = deepest * s - below, above = base - tallest * s - (st->outline ? edge : 0.1f);
        float lift = fminf(lacks, fmaxf(above, 0.0f)) * pc_ui_text_descender_lift;
        if (lift > 0)
            base -= lift, below += lift;
    }
    if (-f->bottom * s > below && below > 0)
        sd = below / -f->bottom;
    if (g.symbol) {
        /* A sign: the digits' size, no taller than the original digits
         * (the font's figures are old style: a % at its cap height towers
         * over them) nor its own original ink, centred on that ink both
         * ways (no descender squeezed). */
        float tall = f->top - f->bottom, inked = fminf((float)(box[3] - box[1]), height) + 0.5f;
        if (tall * s > inked && tall > 0)
            s = inked / tall;
        sx = s * st->stretch;
        if ((f->x1 - f->x0) * sx > room)
            sx = room / (f->x1 - f->x0);
        sd = s;
        if (strchr(":+-", (int)g.codepoint)) /* (signs off the baseline) */
            base = (box[1] + box[3]) * 0.5f + (f->top + f->bottom) * 0.5f * s;
        else /* on the original's baseline: its ink's foot, as the figures beside it */
            base = (float)box[3] + f->bottom * s;
    }
    float cx_t = (box[0] + box[2]) * 0.5f, cx_s = (f->x0 + f->x1) * 0.5f;
    if (st->outline) { /* the ink keeps its outline's room from the box's edges (neighbours overlap them) */
        /* Across, the outline is sx / s as wide (the distances are scaled by s). */
        float side = sx > s ? (edge - pc_ui_text_edge_margin) * sx / s + pc_ui_text_edge_margin : edge;
        if ((f->x1 - f->x0) * sx > box_w - 2.0f * side)
            sx = fmaxf(box_w - 2.0f * side, 0.5f) / (f->x1 - f->x0);
        float half = (f->x1 - f->x0) * sx * 0.5f;
        cx_t = fminf(fmaxf(cx_t, side + half), box_w - side - half);
    }
    float pivot = base - height * 0.5f, bold = st->embolden * SDF_EM;
    /* The disc: centred on the original dot's ink, its radius. */
    float disc_x = (box[0] + box[2]) * 0.5f, disc_y = (box[1] + box[3]) * 0.5f;
    float disc_r = fmaxf(box[2] - box[0], box[3] - box[1]) * 0.5f;
    /* The ramp: the CLUT indices the original glyph uses. */
    int hi = 0;
    for (int y = 0; y < g.box_h; y++)
        for (int x = 0; x < g.box_w; x++) {
            int i = page_index(vram, page_x, g.box_u + x, g.box_v + y);
            hi = i > hi ? i : hi;
        }
    int lo = st->outline ? 2 : 1;
    if (hi < lo)
        hi = lo;
    float stroke = st->outline ? st->stroke : 0.0f;
    int mode = stroke > 0 ? PC_UI_DIST_OUTLINED : PC_UI_DIST_RAMP;
    if (g.symbol && g.style == PC_UI_TEXT_DIGITS) {
        /* A sign from another CLUT (the info style's %): the digits'
         * colours (their CLUT, 832,223: the small 0's brightest inside its
         * outline, index 1) as the nearest of its own, a label's texels
         * (outline into lettering). */
        const uint16_t* own = vram + (clut >> 6 & 511) * 1024 + (clut & 63) * 16;
        const uint16_t* digits = vram + 223 * 1024 + 832;
        int top = 0;
        for (int y = 0; y < 10; y++)
            for (int x = 0; x < 6; x++) {
                int i = page_index(vram, page_x, x, y);
                top = i > top ? i : top;
            }
        if (own != digits && top > 1) {
            int pick[2] = { 1, hi };
            for (int k = 0; k < 2; k++) {
                int want = digits[k ? top : 1], best = 1 << 30;
                for (int i = 1; i < 16; i++) {
                    int c = own[i], d = 0;
                    if (!(c & 0x7fff) && c != want)
                        continue;
                    for (int b = 0; b < 15; b += 5) {
                        int e = (c >> b & 31) - (want >> b & 31);
                        d += e * e;
                    }
                    if (d < best)
                        best = d, pick[k] = i;
                }
            }
            mode = PC_UI_DIST_LABEL, lo = pick[0], hi = pick[1];
        }
    }
    int w = (u1 - u0 + 1) * factor, rows = (v1 - v0 + 1) * factor;
    float inv = 1.0f / factor;
    for (int y = 0; y < rows; y++) {
        float yo = (v0 - g.box_v) + (y + 0.5f) * inv;
        float up = base - yo;
        float fy = up >= 0 ? up / s : up / sd;
        float shift = st->slant * (pivot - yo);
        for (int x = 0; x < w; x++) {
            float xo = (u0 - g.box_u) + (x + 0.5f) * inv;
            float fx = cx_s + (xo - cx_t - shift) / sx;
            float d = bullet ? disc_r - hypotf(xo - disc_x, yo - disc_y) /* original pixels */
                             : (distance_at(f, fx, fy) + bold) * s;
            out[y * w + x] = pc_ui_dist_texel(stroke_distance(d, stroke), mode, lo, hi,
                mode == PC_UI_DIST_LABEL && stroke > 0);
        }
    }
    ++stats.regions;
    return 1;
}

/* ---- string-level layout (ui_text_layout line) ----
 *
 * The game sets its text glyph by glyph in 12 x 12 cells at its own
 * advances (vs_battle_characterWidths: about 6 pixels a letter for menus and
 * the information box, proportional for talk bubbles). A font's letters in
 * those slots are unevenly spaced (narrow letters float in wide slots) and
 * wide ones collide. Here the glyphs of each string the game prints are set
 * again with the font's own advances and kerning:
 *
 * - strings: the game's printers report each character they set
 *   (pc_text_glyph: its sprites' command words, its place and the printer's
 *   own advance); characters set one after another on a line, each where the
 *   previous one left the pen, are one string;
 * - a run: the glyph sprites gpu_hw.c buffers while nothing else is drawn
 *   (a text box's glyphs share one ordering-table slot; menu rows come as 12
 *   strips per glyph, one per row, each with its own brightness);
 * - glyphs: the strips of one cell at one place are one glyph, with its
 *   string (pc_ui_text_piece_call); a glyph no printer reported is placed in
 *   its own cell (place_in_cell, as ui_text_layout glyph places every glyph);
 * - lines: glyphs of one style at one cell top in one drawing area, by x;
 *   a segment is one string's glyphs (a gap of more than three spaces after
 *   the printer's advance, or an overlap, starts another: columns);
 * - a segment starts where the game's first glyph's ink starts; each next
 *   pen is the previous pen + the font's advance, + its kerning or the font's
 *   space (widened: ui_text_word_space) for every space the game left (a gap of 3
 *   pixels or more, 6 each);
 * - it never runs ahead of the game: a glyph's ink ends at most SLACK
 *   pixels after the original glyph's ink (the font tightened where it is
 *   wider);
 * - letters never touch: on every row a glyph's ink stays MIN_GAP from the
 *   previous one's (row by row, so slanted and kerned pairs may share
 *   columns; at these sizes a script face's joins would otherwise run
 *   letters together); this wins over the tightening, and a segment that
 *   then still ends more than SLACK after the game's is condensed until it
 *   does not (fit_segment);
 * - talk bubbles are set on their whole line (from the text box) and, with
 *   ui_text_bubble_align centre, centred within its original extent
 *   (bubble_segments);
 * - each glyph is drawn whole: below the cell (descenders) and beyond its
 *   slot, as the game's own sprite would (same colour, brightness, blending,
 *   rows of a strip-drawn glyph each with that strip's brightness, the rows
 *   below the cell with the last strip's).
 *
 * A glyph's place depends only on the glyphs before it on its line, so text
 * the game types out letter by letter never moves: each glyph appears where
 * it will stay, when the game draws it. Positions snap to the internal
 * resolution's pixels; images are cached in the 2D atlas by content. */
int pc_ui_text_layout = PC_UI_TEXT_LAYOUT_LINE;
const char* const pc_ui_text_layout_names[2] = { "glyph", "line" };
int pc_ui_text_bubble_align = PC_UI_TEXT_ALIGN_CENTRE;
int pc_ui_text_overlay = PC_UI_TEXT_OVERLAY_AUTO;
const char* const pc_ui_text_overlay_names[2] = { "off", "auto" };
const char* const pc_ui_text_bubble_align_names[2] = { "left", "centre" };
#define SLACK 1.0f   /* PS1 pixels a glyph's ink may end after the original's */
#define MIN_GAP 0.5f /* PS1 pixels at least between neighbours' ink on any row */
#define DESCENT_ROWS 6 /* rows below the 12-pixel cell a glyph may reach */
/* The font's space is widened (pc_ui_text_word_space, ui_text_word_space:
 * 1.5): the game's words stand a whole letter apart, and at these sizes
 * (8-pixel capitals) a text face's own space reads as none. */

int pc_ui_text_layout_on(int scale) { return pc_ui_text == PC_UI_TEXT_FONT && scale >= 1; }

/* The printers' characters (pc_text_glyph): by each sprite's command word,
 * kept for a few vsyncs (packets are built and drawn in the same frame). */
#define CALL_SLOTS 8192
#define CALL_PROBES 32
#define CALL_AGE 16 /* vsyncs */
typedef struct {
    const void* command;
    unsigned stamp;    /* vsync it was set at */
    unsigned string;
    int glyph, strip;  /* glyph index (0-377); strip: its row (-1: the whole cell) */
    int x, y;          /* the packet's position (pixels before the drawing offset; a strip's first row) */
    float advance;     /* texels */
} text_call;
static text_call calls[CALL_SLOTS];
static struct {
    int valid, y, next_x;
    unsigned string;
} pen;
static unsigned string_serial;
extern unsigned pc_vsync_count(void) __attribute__((weak));
static unsigned call_now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }

static text_call* call_slot(const void* command, int insert)
{
    unsigned now = call_now(), s = (unsigned)(((uintptr_t)command >> 2) * 2654435761u) & (CALL_SLOTS - 1);
    text_call* free_slot = NULL;
    for (int probe = 0; probe < CALL_PROBES; probe++, s = (s + 1) & (CALL_SLOTS - 1)) {
        text_call* c = &calls[s];
        if (c->command == command)
            return c;
        if (!c->command) {
            if (!free_slot)
                free_slot = c;
            break; /* never used: the end of the chain */
        }
        if (!free_slot && now - c->stamp > CALL_AGE)
            free_slot = c;
    }
    return insert ? (free_slot ? free_slot : &calls[s]) : NULL;
}

/* button_roles.c (battle builds): the glyph shown for the font's ○ and ×
 * where Circle and Cross are exchanged. */
extern int pc_button_roles_glyph(int glyph) __attribute__((weak));

/* Move a glyph's texels `du` across: its sprite, or the fixed font's quad
 * (fixed_font.c: u0 at words 2 and 6, u0 + 12 at 4 and 8). */
static void shift_texels(uint32_t* w, int du)
{
    unsigned code = w[0] >> 24, u = w[2] & 255;
    if ((code & 0xe4) == 0x64)
        w[2] = (w[2] & ~255u) | ((u + (unsigned)du) & 255);
    else if ((w[6] & 255) == u && (w[4] & 255) == u + 12 && (w[8] & 255) == u + 12)
        for (int k = 2; k <= 8; k += 2)
            w[k] = (w[k] & ~255u) | (((w[k] & 255) + (unsigned)du) & 255);
}

#include "oracle.h"
void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance)
{
    /* The game's ○ / × follow the button roles (button_roles.c): the
     * packets show the other glyph, as if the game had set it. */
    if (pc_button_roles_glyph && glyph >= 0) {
        int shown = pc_button_roles_glyph(glyph);
        if (shown != glyph) {
            for (int i = 0; i < count; i++)
                shift_texels(commands[i], (shown - glyph) * 12);
            glyph = shown;
        }
    }
    /* Packet changes above are game state; the capture registry below is not. */
    if (pc_shadow_speculative && pc_shadow_speculative())
        return;
    /* One string: on the line the previous character was set on, where it
     * left the pen (a space or two of offset, 0xFA, allowed either way). */
    int d = x - pen.next_x;
    if (!pen.valid || y != pen.y || d < -(3 << 16) || d > (30 << 16))
        pen.string = ++string_serial ? string_serial : ++string_serial;
    pen.valid = 1, pen.y = y, pen.next_x = next_x;
    for (int i = 0; i < count && glyph >= 0; i++) {
        text_call* c = call_slot(commands[i], 1);
        c->command = commands[i];
        c->stamp = call_now();
        c->string = pen.string;
        c->glyph = glyph, c->strip = count > 1 ? i : -1;
        c->x = x >> 16, c->y = y >> 16;
        c->advance = (float)advance;
    }
}

int pc_ui_text_piece_call(const void* command, pc_ui_text_piece_info* info)
{
    info->string = 0, info->advance = 0;
    const text_call* c = command ? call_slot(command, 0) : NULL;
    if (!c || call_now() - c->stamp > CALL_AGE)
        return 0;
    const uint32_t* w = command;
    int x = (int16_t)(w[1] & 0xffff), y = (int16_t)(w[1] >> 16), u = (int)(w[2] & 255), v = (int)(w[2] >> 8 & 255);
    int row = c->strip < 0 ? 0 : c->strip;
    if (x != c->x || y != c->y + row || u != c->glyph % 21 * 12 || v != c->glyph / 21 * 12 + row)
        return 0; /* another packet at that address since */
    info->string = c->string, info->advance = c->advance;
    return 1;
}

int pc_ui_text_piece(const uint16_t* vram, unsigned tpage, unsigned clut, int u, int v, int w, int h)
{
    glyph_info g;
    if (!vram || w != 12 || h < 1 || h > 12 || !identify(tpage, clut, u, v, u + w - 1, v + h - 1, &g))
        return 0;
    if ((g.style != PC_UI_TEXT_DIALOGUE && g.style != PC_UI_TEXT_MENU) || u != g.box_u)
        return 0;
    const pc_ui_text_style* st = &pc_ui_text_styles[g.style];
    return field(g.style, st->small_caps ? upper(g.codepoint) : g.codepoint) && field(g.style, 'H');
}

typedef struct {
    glyph_info g;
    const glyph_field* f;
    font_file* font;
    int index;               /* the font's glyph (advance, kerning) */
    unsigned drawn;          /* its codepoint (the capital for a small capital) */
    float s, sx;             /* vertical / horizontal scale (PS1 pixels per field pixel) */
    float base, pivot;       /* baseline and slant pivot rows in the cell */
    float adv, x0, x1, desc; /* advance, ink extent from the pen and depth below the baseline (PS1 pixels) */
    int ink[4];              /* the original glyph's ink in its cell */
    int hi;                  /* the highest CLUT index the original uses */
} glyph_metrics;

static int metrics(const uint16_t* vram, unsigned tpage, unsigned clut, int u, int v, int h, glyph_metrics* m)
{
    memset(m, 0, sizeof *m);
    if (!identify(tpage, clut, u, v, u + 11, v + h - 1, &m->g))
        return 0;
    const glyph_info* g = &m->g;
    const pc_ui_text_style* st = &pc_ui_text_styles[g->style];
    int small = st->small_caps && upper(g->codepoint) != g->codepoint;
    m->drawn = small ? upper(g->codepoint) : g->codepoint;
    m->f = field(g->style, m->drawn);
    const glyph_field* cap = field(g->style, 'H');
    m->font = font_for(st->font);
    int page_x = (int)(tpage & 15) * 64, ref[4], xbox[4];
    if (!m->f || !cap || !m->font || cap->top <= 0
        || !ink(vram, page_x, g->box_u, g->box_v, g->box_w, g->box_h, g->threshold, m->ink)
        || !ink(vram, page_x, g->ref_u, g->ref_v, g->ref_w, g->ref_h, g->threshold, ref))
        return 0;
    float base = (float)ref[3], height = (float)(ref[3] - ref[1]);
    if (small && ink(vram, page_x, g->small_u, g->small_v, 12, 12, g->threshold, xbox))
        base = (float)xbox[3], height = (float)(xbox[3] - xbox[1]);
    if (height < 1)
        return 0;
    m->s = height / cap->top;
    m->sx = m->s * st->stretch;
    m->base = base;
    m->pivot = base - height * 0.5f;
    m->index = stbtt_FindGlyphIndex(&m->font->info, (int)m->drawn);
    int advance, bearing;
    stbtt_GetGlyphHMetrics(&m->font->info, m->index, &advance, &bearing);
    m->adv = advance * m->font->scale * m->sx;
    m->x0 = m->f->x0 * m->sx, m->x1 = m->f->x1 * m->sx;
    m->desc = m->f->bottom < 0 ? -m->f->bottom * m->s : 0.0f;
    for (int y = 0; y < g->box_h; y++)
        for (int x = 0; x < g->box_w; x++) {
            int i = page_index(vram, page_x, g->box_u + x, g->box_v + y);
            m->hi = i > m->hi ? i : m->hi;
        }
    return 1;
}

/* A laid-out glyph's image: the glyph at its pen (texel column `left`), cell
 * rows from the top (rows below 12: under the cell). */
typedef struct {
    int style;
    unsigned drawn;
    float s, sx, base, pivot, slant, bold;
    int left, w, h, lo, hi;
    float stroke; /* an outline this wide (PS1 pixels) of CLUT index 1, the ramp from lo inside (0: none) */
} text_image;

#define RUN_GLYPHS 1024
typedef struct {
    glyph_metrics m;
    float x, top, k;   /* the original glyph's left and its cell's top on screen, screen pixels per texel */
    int clip[4];
    unsigned tpage, clut;
    int ok;            /* metrics known */
    unsigned string;   /* the printer's string (0: unknown: drawn glyph by glyph) */
    float advance;     /* the printer's advance after it (texels) */
    float pen;         /* laid out (screen pixels) */
    float sx;          /* its horizontal scale as laid out (PS1 pixels per field pixel) */
    int image;
    int seg;           /* its segment in the line (set_pens; 0: placed in its own cell) */
} run_glyph;
static run_glyph run[RUN_GLYPHS];
static text_image images[RUN_GLYPHS];
static int image_count;

/* The lines report (RPC ui_text lines): the newest frame's laid-out glyphs. */
#define REPORT_GLYPHS 1024
typedef struct {
    int line, segment;     /* line and segment (a run of glyphs set together) numbers in the frame */
    unsigned codepoint;
    float x, top, k;       /* the original glyph: left, cell top, scale */
    float advance;         /* the printer's advance (texels) */
    unsigned string;       /* the printer's string */
    float ink[4];          /* its ink (screen) */
    float pen, quad_x;     /* laid out: the pen and the image's left edge on screen */
    text_image image;
} report_glyph;
static report_glyph report[REPORT_GLYPHS];
static int report_count, report_lines, report_segments, report_stale = 1;
static int report_unexplained; /* glyphs no printer reported (drawn glyph by glyph) */
void pc_ui_text_frame(void) { report_stale = 1; }

/* A glyph's ink per field row (field pixels from its origin). */
static const float* profile(glyph_field* g)
{
    if (!g->profile) {
        int y0 = (int)floorf(g->bottom) - 1, rows = (int)ceilf(g->top) + 2 - y0;
        float* p = malloc(sizeof *p * 2 * (size_t)(rows > 0 ? rows : 1));
        if (!p)
            return NULL;
        for (int r = 0; r < rows; r++) {
            float lo = 1e9f, hi = -1e9f;
            for (float x = g->x0 - 2; x <= g->x1 + 2; x += 0.25f)
                if (distance_at(g, x, y0 + r + 0.5f) > 0)
                    lo = fminf(lo, x), hi = fmaxf(hi, x);
            p[r * 2] = lo, p[r * 2 + 1] = hi;
        }
        g->profile_y0 = y0, g->profile_rows = rows;
        g->profile = p;
    }
    return g->profile;
}
/* The least pen distance (texels) from glyph p to the next glyph q at which
 * their ink stays MIN_GAP apart on every row (both sheared alike: the slant
 * cancels on a row, only the rows' places in each glyph differ). */
static float apart(const glyph_metrics* p, const glyph_metrics* q, float slant)
{
    glyph_field *fp = (glyph_field*)p->f, *fq = (glyph_field*)q->f;
    const float *a = profile(fp), *b = profile(fq);
    float need = p->x1 - q->x0 + MIN_GAP; /* (without profiles: the boxes) */
    if (!a || !b)
        return need;
    need = -1e9f;
    for (float yo = 0.125f; yo < 12 + DESCENT_ROWS; yo += 0.25f) {
        int ra = (int)floorf((p->base - yo) / p->s) - fp->profile_y0;
        int rb = (int)floorf((q->base - yo) / q->s) - fq->profile_y0;
        if (ra < 0 || rb < 0 || ra >= fp->profile_rows || rb >= fq->profile_rows || a[ra * 2] > a[ra * 2 + 1]
            || b[rb * 2] > b[rb * 2 + 1])
            continue;
        float shift_p = slant * (p->pivot - yo), shift_q = slant * (q->pivot - yo);
        float right = a[ra * 2 + 1] * p->sx + shift_p, left = b[rb * 2] * q->sx + shift_q;
        need = fmaxf(need, right - left + MIN_GAP);
    }
    return need > -1e8f ? need : p->x1 - q->x0 + MIN_GAP;
}

static int glyph_order(const void* a, const void* b)
{
    const run_glyph *p = &run[*(const int*)a], *q = &run[*(const int*)b];
    if (p->m.g.style != q->m.g.style)
        return p->m.g.style - q->m.g.style;
    if (p->k != q->k)
        return p->k < q->k ? -1 : 1;
    if (p->top != q->top)
        return p->top < q->top ? -1 : 1;
    int c = memcmp(p->clip, q->clip, sizeof p->clip);
    if (c)
        return c;
    if (p->x != q->x)
        return p->x < q->x ? -1 : 1;
    return *(const int*)a - *(const int*)b;
}

/* A glyph in its own cell (ui_text_layout glyph, and glyphs no printer
 * reported): placed as pc_ui_text_render places it (centred on the
 * original's ink, condensed to the game's advance where wider), but drawn
 * whole: its descenders below the cell. */
static void place_in_cell(run_glyph* r)
{
    const glyph_metrics* m = &r->m;
    float advance = r->advance > 0 ? r->advance : m->g.advance ? (float)m->g.advance : 12.0f;
    float room = fmaxf((float)(m->ink[2] - m->ink[0]), advance) + 0.5f, wide = m->f->x1 - m->f->x0;
    r->sx = wide * m->sx > room ? room / wide : m->sx;
    float cx_t = (m->ink[0] + m->ink[2]) * 0.5f, cx_s = (m->f->x0 + m->f->x1) * 0.5f;
    r->pen = r->x + (cx_t - cx_s * r->sx) * r->k;
    r->seg = 0;
}

/* The pens of one line's glyphs (by x), segment by segment (r->seg, from
 * seg0 + 1); PC_UI_TEXT_LAYOUT_GLYPH: each in its own cell. */
static void set_pens(run_glyph* const* line, int n, float slant, int mode, int seg0)
{
    run_glyph* prev = NULL;
    int segment = seg0;
    for (int j = 0; j < n; j++) {
        run_glyph* r = line[j];
        if (!r->ok)
            continue;
        if (!r->string || mode == PC_UI_TEXT_LAYOUT_GLYPH) {
            prev = NULL; /* in its cell: the next glyph starts afresh */
            place_in_cell(r);
            continue;
        }
        r->sx = r->m.sx;
        const glyph_metrics* m = &r->m;
        float k = r->k;
        /* Ink ends at most SLACK after the original's. */
        float cap = r->x + (m->ink[2] + SLACK - m->x1) * k;
        if (prev) {
            const glyph_metrics* q = &prev->m;
            /* The printer's own advance: what is left over are its spaces. */
            float gap = (r->x - prev->x) / k - prev->advance;
            if (prev->string != r->string || gap < -1.5f || gap > 18.5f)
                prev = NULL; /* another string or a column */
            else {
                int spaces = gap >= 2.5f ? (int)lrintf(gap / 6.0f) : 0;
                float pen = prev->pen + q->adv * k;
                if (spaces) {
                    int space = stbtt_FindGlyphIndex(&m->font->info, ' '), w, lsb;
                    stbtt_GetGlyphHMetrics(&m->font->info, space, &w, &lsb);
                    pen += spaces * w * m->font->scale * fmaxf(q->sx, m->sx) * pc_ui_text_word_space * k;
                } else if (q->font == m->font)
                    pen += stbtt_GetGlyphKernAdvance(&m->font->info, q->index, m->index) * m->font->scale
                         * (q->sx + m->sx) * 0.5f * k;
                /* Tightened where the font runs ahead, never into the previous glyph. */
                r->pen = fmaxf(fminf(pen, cap), prev->pen + apart(q, m, slant) * k);
            }
        }
        if (!prev) { /* a segment's first glyph: its ink starts where the original's does */
            r->pen = r->x + (m->ink[0] - m->x0) * k;
            ++segment;
        }
        r->seg = segment;
        prev = r;
    }
}

/* ui_text_bubble_align centre: the shift that centres glyphs [a, b) (one
 * segment, set) within the original's extent of the same glyphs. */
static float centring(run_glyph* const* line, int a, int b)
{
    const run_glyph *p = line[a], *q = line[b - 1];
    float o0 = p->x + p->m.ink[0] * p->k, o1 = q->x + q->m.ink[2] * q->k;
    float l0 = p->pen + p->m.x0 * p->k, l1 = q->pen + q->m.x1 * q->k;
    return ((o0 + o1) - (l0 + l1)) * 0.5f;
}

/* A glyph's metrics `c` times as wide. */
static void condense(glyph_metrics* m, float c)
{
    m->sx *= c, m->adv *= c, m->x0 *= c, m->x1 *= c;
}

/* Segment [a, b) (set) condensed until its ink ends at most SLACK after the
 * original's: where the font's letters are wider than the game's, keeping
 * them from touching (apart) can run a line long. */
static void fit_segment(run_glyph* const* line, int a, int b, float slant)
{
    for (int pass = 0; pass < 6; pass++) {
        const run_glyph *p = line[a], *q = line[b - 1];
        float end = q->x + (q->m.ink[2] + SLACK) * q->k;
        float l0 = p->pen + p->m.x0 * p->k, l1 = q->pen + q->m.x1 * q->k;
        if (l1 <= end + 0.01f || l1 <= l0)
            return;
        float c = fmaxf((end - l0) / (l1 - l0), 0.5f);
        for (int j = a; j < b; j++)
            condense(&line[j]->m, c);
        set_pens(line + a, b - a, slant, PC_UI_TEXT_LAYOUT_LINE, line[a]->seg - 1);
    }
}

/* The game's whole line (text_lines.c, battle builds): each glyph's index
 * and x, from the text box that draws it. */
extern int pc_text_box_line(int x, int y, int glyph, int* glyphs, int* xs, int max) __attribute__((weak));

/* Talk bubbles: each segment of a line fitted (fit_segment) and, with
 * ui_text_bubble_align centre, centred within the original's extent of its
 * glyphs. A bubble types its page out letter by letter, so the glyphs drawn
 * are often only the start of the line: the whole line comes from the
 * game's text box (the page walked as the game walks it) and is set and
 * fitted in full, so each letter is drawn where it will stay. A line no box
 * explains (a zooming bubble draws whole lines) is set as drawn. */
static void bubble_segments(const uint16_t* vram, run_glyph* const* line, int n, float slant, int centre)
{
    static run_glyph whole[128];
    static run_glyph* list[128];
    for (int a = 0; a < n;) {
        if (!line[a]->ok || !line[a]->seg) {
            ++a;
            continue;
        }
        int b = a + 1;
        while (b < n && line[b]->ok && line[b]->seg == line[a]->seg)
            ++b;
        const run_glyph* first = line[a];
        int ids[128], xs[128], count = 0, done = 0;
        float shift = 0;
        if (pc_text_box_line && first->k == 1.0f && a == 0)
            count = pc_text_box_line((int)first->x - first->clip[0], (int)first->top - first->clip[1],
                first->m.g.box_v / 12 * 21 + first->m.g.box_u / 12, ids, xs, 128);
        int prefix = count >= b - a;
        for (int j = 0; prefix && j < b - a; j++)
            prefix = ids[j] == line[a + j]->m.g.box_v / 12 * 21 + line[a + j]->m.g.box_u / 12
                  && xs[j] + first->clip[0] == (int)line[a + j]->x;
        if (prefix) {
            /* The whole line, set as the drawn glyphs are. */
            int m = 0;
            for (int j = 0; j < count; j++) {
                run_glyph* r = &whole[m];
                if (j < b - a)
                    *r = *line[a + j];
                else {
                    memset(r, 0, sizeof *r);
                    r->ok = metrics(vram, first->tpage, first->clut, ids[j] % 21 * 12, ids[j] / 21 * 12, 12, &r->m);
                    r->x = (float)(xs[j] + first->clip[0]), r->top = first->top, r->k = 1.0f;
                    r->string = first->string;
                    r->advance = r->m.g.advance ? (float)r->m.g.advance : 12.0f; /* (the game's table: a text box) */
                    if (!r->ok)
                        break; /* (a pictograph: the line as far as it is set) */
                }
                list[m++] = r;
            }
            set_pens(list, m, slant, PC_UI_TEXT_LAYOUT_LINE, 0);
            int end = 1;
            while (end < m && list[end]->seg == list[0]->seg)
                ++end;
            if (end >= b - a) {
                fit_segment(list, 0, end, slant);
                for (int j = 0; j < b - a; j++) {
                    line[a + j]->m = list[j]->m;
                    line[a + j]->sx = list[j]->sx;
                    line[a + j]->pen = list[j]->pen;
                }
                shift = centre ? centring(list, 0, end) : 0.0f;
                done = 1;
            }
        }
        if (!done) {
            fit_segment(line, a, b, slant);
            shift = centre ? centring(line, a, b) : 0.0f;
        }
        for (int j = a; j < b; j++)
            line[j]->pen += shift;
        a = b;
    }
}

void pc_ui_text_layout_run(const uint16_t* vram, const pc_ui_text_piece_info* pieces, int count, int scale, int mode,
    pc_ui_text_quad* out)
{
    int glyphs = 0, last = -1;
    static int of_piece[4096];
    int reported = mode == (pc_ui_text_layout ? PC_UI_TEXT_LAYOUT_LINE : PC_UI_TEXT_LAYOUT_GLYPH);
    image_count = 0;
    if (report_stale && reported)
        report_count = report_lines = report_segments = report_unexplained = 0, report_stale = 0;
    /* Glyphs: the pieces of one cell at one place. */
    for (int i = 0; i < count; i++) {
        const pc_ui_text_piece_info* p = &pieces[i];
        out[i].image = -1;
        of_piece[i & 4095] = -1;
        glyph_info g;
        if (i >= 4096 || !identify(p->tpage, p->clut, p->u, p->v, p->u + 11, p->v + p->h - 1, &g))
            continue;
        float top = p->y - (p->v - g.box_v) * p->k;
        int found = -1;
        for (int k = last >= 0 ? last : glyphs - 1, n = 0; n < glyphs; n++, k = k ? k - 1 : glyphs - 1) {
            const run_glyph* r = &run[k];
            if (r->x == p->x && r->top == top && r->k == p->k && r->m.g.box_u == g.box_u && r->m.g.box_v == g.box_v
                && r->m.g.style == g.style && r->clut == p->clut && r->tpage == p->tpage
                && !memcmp(r->clip, p->clip, sizeof r->clip)) {
                found = k;
                break;
            }
        }
        if (found < 0) {
            if (glyphs == RUN_GLYPHS)
                continue;
            run_glyph* r = &run[glyphs];
            memset(r, 0, sizeof *r);
            r->ok = metrics(vram, p->tpage, p->clut, g.box_u, g.box_v, 12, &r->m);
            r->x = p->x, r->top = top, r->k = p->k, r->tpage = p->tpage, r->clut = p->clut;
            r->string = p->string, r->advance = p->advance;
            r->m.g = g; /* (identity even when the metrics failed) */
            memcpy(r->clip, p->clip, sizeof r->clip);
            r->image = -1;
            found = glyphs++;
        }
        if (run[found].string != p->string)
            run[found].string = 0; /* its pieces disagree: glyph by glyph */
        last = found;
        of_piece[i] = found;
    }
    /* Lines and segments, each set from its start (in texels of the
     * glyphs' cells: k screen pixels each). */
    static int order[RUN_GLYPHS];
    static run_glyph* line[RUN_GLYPHS];
    for (int k = 0; k < glyphs; k++)
        order[k] = k;
    qsort(order, (size_t)glyphs, sizeof *order, glyph_order);
    for (int a = 0; a < glyphs;) {
        int b = a + 1;
        const run_glyph* first = &run[order[a]];
        while (b < glyphs && run[order[b]].m.g.style == first->m.g.style && run[order[b]].top == first->top
            && run[order[b]].k == first->k && !memcmp(run[order[b]].clip, first->clip, sizeof first->clip))
            ++b;
        const pc_ui_text_style* st = &pc_ui_text_styles[first->m.g.style];
        float bold = st->embolden * SDF_EM, k = first->k;
        for (int j = a; j < b; j++)
            line[j - a] = &run[order[j]];
        set_pens(line, b - a, st->slant, mode, 0);
        if (mode == PC_UI_TEXT_LAYOUT_LINE && first->m.g.style == PC_UI_TEXT_DIALOGUE)
            bubble_segments(vram, line, b - a, st->slant, pc_ui_text_bubble_align == PC_UI_TEXT_ALIGN_CENTRE);
        else if (mode == PC_UI_TEXT_LAYOUT_LINE)
            for (int j = 0; j < b - a;) { /* each segment fitted */
                int e = j + 1;
                if (line[j]->ok && line[j]->seg) {
                    while (e < b - a && line[e]->ok && line[e]->seg == line[j]->seg)
                        ++e;
                    fit_segment(line, j, e, st->slant);
                }
                j = e;
            }
        int segment = -1;
        for (int j = a; j < b; j++) {
            run_glyph* r = &run[order[j]];
            if (r->ok && !r->string && reported)
                ++report_unexplained;
            if (!r->ok)
                continue; /* (a pictograph: its pieces draw the original) */
            if (r->seg != segment || !r->seg)
                segment = r->seg, ++report_segments;
            const glyph_metrics* m = &r->m;
            /* Its image. */
            text_image* im = &images[image_count];
            memset(im, 0, sizeof *im);
            im->style = m->g.style;
            im->drawn = m->drawn;
            im->s = m->s, im->sx = r->sx, im->base = m->base, im->pivot = m->pivot;
            im->slant = st->slant, im->bold = bold;
            float lean_lo = fminf(0.0f, st->slant * (m->pivot - (12 + DESCENT_ROWS))),
                  lean_hi = fmaxf(0.0f, st->slant * m->pivot);
            im->left = (int)ceilf(-(m->f->x0 * r->sx + lean_lo)) + 2;
            im->w = im->left + (int)ceilf(m->f->x1 * r->sx + lean_hi) + 2;
            im->h = (int)ceilf(m->base + m->desc + 1.0f);
            im->h = im->h < 12 ? 12 : im->h > 12 + DESCENT_ROWS ? 12 + DESCENT_ROWS : im->h;
            im->lo = 1;
            im->hi = m->hi < 1 ? 1 : m->hi;
            if (im->w <= 0 || im->w > 255)
                continue;
            r->image = image_count++;
            if (reported && report_count < REPORT_GLYPHS) {
                report_glyph* e = &report[report_count++];
                e->line = report_lines, e->segment = report_segments;
                e->codepoint = m->g.codepoint;
                e->x = r->x, e->top = r->top, e->k = k, e->advance = r->advance, e->string = r->string;
                e->ink[0] = r->x + m->ink[0] * k, e->ink[1] = r->top + m->ink[1] * k;
                e->ink[2] = r->x + m->ink[2] * k, e->ink[3] = r->top + m->ink[3] * k;
                e->pen = r->pen;
                e->quad_x = r->pen - im->left * k;
                e->image = *im;
            }
        }
        report_lines += reported;
        a = b;
    }

    /* Each piece's quad: its rows of its glyph's image (the cell's last row
     * also draws the rows below the cell). */
    for (int i = 0; i < count && i < 4096; i++) {
        int j = of_piece[i];
        if (j < 0 || run[j].image < 0)
            continue;
        const run_glyph* r = &run[j];
        const text_image* im = &images[r->image];
        pc_ui_text_quad* q = &out[i];
        q->image = r->image;
        q->k = r->k;
        q->x = r->pen - im->left * r->k;
        q->y = pieces[i].y;
        q->w = im->w, q->h = im->h;
        q->row0 = pieces[i].v - r->m.g.box_v;
        q->row1 = q->row0 + pieces[i].h;
        if (q->row1 >= 12)
            q->row1 = im->h;
    }
}

static uint64_t image_hash(const text_image* im)
{
    const unsigned char* b = (const unsigned char*)im;
    uint64_t h = 1469598103934665603ull ^ pc_ui_text_generation;
    for (size_t i = 0; i < sizeof *im; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h | 1;
}

static const text_image* status_image(int image);
int pc_ui_text_image_key(int image, uint64_t* key, int* w, int* h)
{
    const text_image* im = status_image(image);
    if (!im && (image < 0 || image >= image_count))
        return 0;
    if (!im)
        im = &images[image];
    *key = image_hash(im);
    *w = im->w, *h = im->h;
    return 1;
}

/* Coverage of image `im` at texel point (xo, yo) as a signed distance (PS1 pixels, positive inside). */
static float image_distance(const text_image* im, const glyph_field* f, float xo, float yo)
{
    float up = im->base - yo, shift = im->slant * (im->pivot - yo);
    float fx = (xo - im->left - shift) / im->sx, fy = up / im->s;
    return (distance_at(f, fx, fy) + im->bold) * im->s;
}

static int draw_image(const text_image* im, int factor, uint32_t* out)
{
    const glyph_field* f = field(im->style, im->drawn);
    if (!f)
        return 0;
    int w = im->w * factor, rows = im->h * factor;
    float inv = 1.0f / factor;
    int mode = im->stroke > 0 ? PC_UI_DIST_OUTLINED : PC_UI_DIST_RAMP;
    for (int y = 0; y < rows; y++) {
        float yo = (y + 0.5f) * inv;
        for (int x = 0; x < w; x++)
            out[y * w + x] = pc_ui_dist_texel(stroke_distance(image_distance(im, f, (x + 0.5f) * inv, yo), im->stroke),
                mode, im->lo, im->hi, 0);
    }
    return 1;
}

static const text_image* status_image(int image);
int pc_ui_text_image_render(int image, int factor, uint32_t* out)
{
    const text_image* status = status_image(image);
    if (status) {
        if (!draw_image(status, factor, out)) {
            ++stats.failed;
            return 0;
        }
        ++stats.regions;
        return 1;
    }
    if (image < 0 || image >= image_count || !draw_image(&images[image], factor, out)) {
        ++stats.failed;
        return 0;
    }
    ++stats.regions;
    return 1;
}

/* ---- the battle's status words (pc_text_status) ----
 *
 * BATTLE.PRG prints its timing words (func_800958A4: TOO FAST!, FAST!,
 * SLOW!, TOO SLOW!, and COOL!, GOOD!, ... for a well-timed chain) and the
 * chain count (func_80095A4C: 3 CHAINS) with vs_battle_renderTextRawColor:
 * a sprite per glyph (spaces too, blank) of the info style's capitals, in
 * the game's colour, centred on a point. Its ROOD_PC hooks hand over the
 * packets of each call; with ui_text font the string is set as one word in
 * the status style (font, size, stretch, slant, embolden, stroke, tracking:
 * settings), centred on the original glyphs' ink, and each glyph's
 * sprite draws its glyph's image there (gpu_hw.c), above and below its
 * 10-row box as the size needs. Colours, brightness, places, timing and
 * motion stay the game's. */
#define STATUS_STRINGS 16
#define STATUS_GLYPHS 24
#define STATUS_SCREEN_W 320.0f       /* the field's display width (the words' place is the game's, on it) */
#define STATUS_IMAGE_BASE (1 << 20) /* image ids: STATUS_IMAGE_BASE + string * STATUS_GLYPHS + glyph */
typedef struct {
    unsigned stamp; /* vsync it was printed at */
    int count;
    unsigned tpage;
    struct {
        const void* command; /* its sprite's command word */
        int x, y, u, v, w;
        unsigned clut;
        int space;
    } glyph[STATUS_GLYPHS];
    unsigned generation; /* laid out for this style generation (0: not yet) */
    int ok;
    float dx[STATUS_GLYPHS], dy; /* each glyph's image's top-left from its sprite's */
    text_image image[STATUS_GLYPHS];
    char text[STATUS_GLYPHS + 1];
    float extent[4], original[4]; /* laid-out and original ink (x0, y0, x1, y1) from the first sprite's place */
    pc_text_typeset typeset;      /* a classic draw's (pc/src/classic/typeset.h); set 0: the defaults below */
    int style;                    /* PC_UI_TEXT_STATUS, or PC_UI_TEXT_INFO: a small-capital word (info_words) */
    /* Each letter's ink across as laid out, as the font's own advances and
     * kerning alone would set it (scaled as laid out), and as it is drawn in
     * its own box (glyph by glyph: pc_ui_text_render), from the first
     * sprite's place (RPC ui_text words: tests). */
    float ink_x[STATUS_GLYPHS][2], font_x[STATUS_GLYPHS][2], cell_x[STATUS_GLYPHS][2];
    float tracking, condensed;    /* the tracking added (PS1 pixels a gap) and the condensing (x) of a small-capital word */
} status_string;
static status_string statuses[STATUS_STRINGS];
static int status_next;
static long status_glyphs_drawn; /* glyph quads handed to gpu_hw (RPC) */

void pc_text_status(const void* first, const void* end) { pc_text_status_typeset(first, end, NULL); }
void pc_text_status_typeset(const void* first, const void* end, const pc_text_typeset* typeset)
{
    const uint32_t *w = first, *stop = end;
    if (!w || !stop || stop <= w || stop - w > 8 * STATUS_GLYPHS)
        return;
    status_string* s = &statuses[status_next];
    memset(s, 0, sizeof *s);
    if (typeset)
        s->typeset = *typeset;
    s->style = PC_UI_TEXT_STATUS;
    s->tpage = 12;
    while (w + 1 < stop && s->count < STATUS_GLYPHS) {
        unsigned len = w[0] >> 24, code = w[1] >> 24;
        if (len && code == 0xe1)
            s->tpage = w[1] & 0x7ff;
        else if (len == 4 && (code & 0xfc) == 0x64 && w + 5 <= stop) {
            s->glyph[s->count].command = w + 1;
            s->glyph[s->count].x = (int16_t)(w[2] & 0xffff), s->glyph[s->count].y = (int16_t)(w[2] >> 16);
            s->glyph[s->count].u = (int)(w[3] & 255), s->glyph[s->count].v = (int)(w[3] >> 8 & 255);
            s->glyph[s->count].clut = w[3] >> 16, s->glyph[s->count].w = (int)(w[4] & 0xffff);
            ++s->count;
        }
        w += 1 + len;
    }
    if (!s->count)
        return;
    s->stamp = call_now();
    status_next = (status_next + 1) % STATUS_STRINGS;
}

/* Small-capital words (info_words): every string the game's small-capital
 * printer (BATTLE.PRG vs_battle_renderTextRawColor: menu labels and
 * headings such as WEAPON, HEAD, UNDEAD, ATTACK/STR, ORG/EQP) sets is a
 * word, found from any of its sprites (find_word), set as the status
 * words are in the info style, and kept within the game's width. */
#define WORDS 64
#define WORD_IMAGE_BASE (STATUS_IMAGE_BASE + STATUS_STRINGS * STATUS_GLYPHS)
static status_string words[WORDS];
static int word_next;
static long word_glyphs_drawn; /* glyph quads handed to gpu_hw (RPC) */

static const text_image* status_image(int image)
{
    int i = image - STATUS_IMAGE_BASE;
    if (i >= STATUS_STRINGS * STATUS_GLYPHS && i - STATUS_STRINGS * STATUS_GLYPHS < WORDS * STATUS_GLYPHS) {
        i -= STATUS_STRINGS * STATUS_GLYPHS;
        const status_string* s = &words[i / STATUS_GLYPHS];
        return s->ok && i % STATUS_GLYPHS < s->count && !s->glyph[i % STATUS_GLYPHS].space
                 ? &s->image[i % STATUS_GLYPHS]
                 : NULL;
    }
    if (i < 0 || i >= STATUS_STRINGS * STATUS_GLYPHS)
        return NULL;
    const status_string* s = &statuses[i / STATUS_GLYPHS];
    return s->ok && i % STATUS_GLYPHS < s->count && !s->glyph[i % STATUS_GLYPHS].space ? &s->image[i % STATUS_GLYPHS]
                                                                                       : NULL;
}

static int status_layout(const uint16_t* vram, status_string* s)
{
    int style = s->style, word = style == PC_UI_TEXT_INFO;
    const pc_ui_text_style* st = &pc_ui_text_styles[style];
    font_file* f = font_for(st->font);
    const glyph_field* cap = field(style, 'H');
    if (!f || !cap || cap->top <= 0)
        return 0;
    int page_x = (int)(s->tpage & 15) * 64, prev = 0, n = 0;
    float base = 0, height = 0, pen[STATUS_GLYPHS] = { 0 }, x = 0, top = 0, depth = 0;
    float l0 = 1e9f, l1 = -1e9f, o0 = 1e9f, o1 = -1e9f, oy0 = 1e9f, oy1 = -1e9f;
    float s0 = 0, sc = 0, sx = 0, track = 0;
    const glyph_field* fl[STATUS_GLYPHS] = { 0 };
    int hi[STATUS_GLYPHS] = { 0 }, chars = 0;
    unsigned drawn[STATUS_GLYPHS] = { 0 };
    int space_u, space_v, space_w;
    int have_space = small_find(' ', &space_u, &space_v, &space_w);
    /* The decisions: a classic draw's (its plugin's look table), else these
     * defaults (the game's own drawing: the fallback). */
    const pc_text_typeset* ts = &s->typeset;
    int blanks_one = ts->set & PC_TYPESET_BLANKS ? ts->blanks != 0 : 1;          /* look-default */
    int keep_on_screen = ts->set & PC_TYPESET_KEEP ? ts->keep_on_screen != 0 : !word; /* look-default */
    float cell_cx[STATUS_GLYPHS] = { 0 }; /* a small-capital word's letters: the original's ink middle */
    float screen_w = ts->set & PC_TYPESET_SCREEN_W ? ts->screen_w : STATUS_SCREEN_W;
    float margin = ts->set & PC_TYPESET_MARGIN ? ts->margin : pc_ui_text_edge_margin;
    float word_space = ts->set & PC_TYPESET_WORD_SPACE ? ts->word_space : pc_ui_text_word_space;
    for (int i = 0; i < s->count; i++) {
        glyph_info g;
        int box[4], u = s->glyph[i].u, v = s->glyph[i].v, w = s->glyph[i].w;
        float rel = (float)(s->glyph[i].x - s->glyph[0].x);
        if (have_space && u == space_u && v == space_v && w == space_w) {
            s->glyph[i].space = 1;
            if (!prev && blanks_one)
                continue; /* the game pads its words with narrow blanks (3 between WELL and TIMED!): one space */
            int space = stbtt_FindGlyphIndex(&f->info, ' '), advance, bearing;
            stbtt_GetGlyphHMetrics(&f->info, space, &advance, &bearing);
            if (sx > 0)
                x += advance * f->scale * sx * word_space + track;
            prev = 0;
            if (chars < STATUS_GLYPHS)
                s->text[chars++] = ' ';
            continue;
        }
        if (!identify(s->tpage, s->glyph[i].clut, u, v, u + w - 1, v + 9, &g) || g.style != PC_UI_TEXT_INFO
            || !ink(vram, page_x, g.box_u, g.box_v, g.box_w, g.box_h, g.threshold, box))
            return 0;
        if (!n) { /* the original's cap height and baseline (its H), the scale */
            int ref[4];
            if (!ink(vram, page_x, g.ref_u, g.ref_v, g.ref_w, g.ref_h, g.threshold, ref) || ref[3] <= ref[1])
                return 0;
            base = (float)ref[3], height = (float)(ref[3] - ref[1]);
            s0 = height / cap->top;
            sc = s0 * (st->size > 0.1f ? st->size : 0.1f);
            sx = sc * st->stretch;
            track = st->tracking * SDF_EM * sx;
        }
        unsigned cp = st->small_caps ? upper(g.codepoint) : g.codepoint;
        if (!(fl[i] = field(style, cp)))
            return 0;
        cell_cx[i] = rel + (box[0] + box[2]) * 0.5f;
        drawn[i] = cp;
        int index = stbtt_FindGlyphIndex(&f->info, (int)cp), advance, bearing;
        if (prev)
            x += stbtt_GetGlyphKernAdvance(&f->info, prev, index) * f->scale * sx + track;
        pen[i] = x;
        l0 = fminf(l0, x + fl[i]->x0 * sx), l1 = fmaxf(l1, x + fl[i]->x1 * sx);
        top = fmaxf(top, fl[i]->top), depth = fmaxf(depth, -fl[i]->bottom);
        o0 = fminf(o0, rel + box[0]), o1 = fmaxf(o1, rel + box[2]);
        oy0 = fminf(oy0, (float)box[1]), oy1 = fmaxf(oy1, (float)box[3]);
        stbtt_GetGlyphHMetrics(&f->info, index, &advance, &bearing);
        x += advance * f->scale * sx;
        prev = index;
        for (int yy = 0; yy < g.box_h; yy++)
            for (int xx = 0; xx < g.box_w; xx++) {
                int k = page_index(vram, page_x, g.box_u + xx, g.box_v + yy);
                hi[i] = k > hi[i] ? k : hi[i];
            }
        if (chars < STATUS_GLYPHS)
            s->text[chars++] = cp >= 0x20 && cp < 0x7f && cp != '"' && cp != '\\' ? (char)cp : '?';
        ++n;
    }
    if (!n || l1 <= l0)
        return 0;
    for (int i = 0; i < s->count; i++)
        if (fl[i])
            s->font_x[i][0] = pen[i] + fl[i]->x0 * sx, s->font_x[i][1] = pen[i] + fl[i]->x1 * sx;
    s->tracking = 0, s->condensed = 1;
    if (word) {
        /* A small-capital word keeps the game's width: condensed where the
         * font runs more than ui_text_info_slack past it, widened by up to
         * ui_text_info_tracking a gap where it is narrower (the font's own
         * spacing, evenly opened; the rest centred). */
        float natural = l1 - l0, game = o1 - o0;
        if (natural > game + pc_ui_text_info_slack) {
            float c = (game + pc_ui_text_info_slack) / natural;
            for (int i = 0; i < s->count; i++)
                pen[i] *= c, s->font_x[i][0] *= c, s->font_x[i][1] *= c;
            l0 *= c, l1 *= c, sx *= c;
            s->condensed = c;
        } else if (natural < game && n > 1) {
            float t = fminf((game - natural) / (float)(n - 1), pc_ui_text_info_tracking);
            for (int i = 0, j = 0; i < s->count; i++)
                if (fl[i])
                    pen[i] += t * (float)j++;
            l1 += t * (float)(n - 1);
            s->tracking = t;
        }
        /* The font's letters (as condensed), each centred in the
         * original's own box as glyph by glyph draws them (for the report:
         * what the word replaces). */
        for (int i = 0; i < s->count; i++)
            if (fl[i]) {
                float half = (fl[i]->x1 - fl[i]->x0) * sx * 0.5f;
                s->cell_x[i][0] = cell_cx[i] - half, s->cell_x[i][1] = cell_cx[i] + half;
            }
    }
    /* Centred on the original's ink across, grown about its cap height's
     * middle; each image the glyph with its stroke and edge around it. */
    float shift = (o0 + o1 - l0 - l1) * 0.5f, mid = base - height * 0.5f;
    float new_base = mid + height * (sc / s0) * 0.5f;
    float stroke = st->outline ? st->stroke : 0.0f, pad = ceilf(stroke + 1.5f), bold = st->embolden * SDF_EM;
    /* Wider than the game's word (the font, its size), centred on it, a
     * word near the screen's edge (WELL TIMED! from x 5) would leave it:
     * kept on the PS1's 320-pixel screen, its outline and that outline's
     * edge (ui_text_edge_margin) included (16:9 anchors the HUD at the
     * window's edges, so the same holds there). */
    float room = (st->outline ? stroke : 0.0f) + margin + fmaxf(bold, 0.0f) * sc;
    float at = (float)s->glyph[0].x, lo_x = room - at, hi_x = screen_w - room - at;
    if (keep_on_screen && shift + l1 > hi_x)
        shift = hi_x - l1;
    if (keep_on_screen && shift + l0 < lo_x)
        shift = lo_x - l0;
    float t = floorf(new_base - top * sc - pad), b = ceilf(new_base + depth * sc + pad);
    int lo = st->outline ? 2 : 1;
    for (int i = 0; i < s->count; i++) {
        if (s->glyph[i].space)
            continue;
        text_image* im = &s->image[i];
        memset(im, 0, sizeof *im);
        im->style = style;
        im->drawn = drawn[i];
        im->s = sc, im->sx = sx, im->base = new_base - t, im->pivot = mid - t;
        im->slant = st->slant, im->bold = bold, im->stroke = stroke;
        float lean_lo = fminf(0.0f, st->slant * (im->pivot - (b - t))), lean_hi = fmaxf(0.0f, st->slant * im->pivot);
        im->left = (int)ceilf(-(fl[i]->x0 * sx + lean_lo) + pad);
        im->w = im->left + (int)ceilf(fl[i]->x1 * sx + lean_hi + pad);
        im->h = (int)(b - t);
        im->lo = lo, im->hi = hi[i] > lo ? hi[i] : lo;
        if (im->w <= 0 || im->w > 255 || im->h <= 0 || im->h > 255)
            return 0;
        s->dx[i] = shift + pen[i] - (float)(s->glyph[i].x - s->glyph[0].x) - (float)im->left;
        s->ink_x[i][0] = shift + pen[i] + fl[i]->x0 * sx, s->ink_x[i][1] = shift + pen[i] + fl[i]->x1 * sx;
        s->font_x[i][0] += shift, s->font_x[i][1] += shift;
    }
    s->dy = t;
    s->extent[0] = shift + l0, s->extent[2] = shift + l1;
    s->extent[1] = new_base - top * sc, s->extent[3] = new_base + depth * sc;
    s->original[0] = o0, s->original[1] = oy0, s->original[2] = o1, s->original[3] = oy1;
    return 1;
}

int pc_ui_text_status_quad(const uint16_t* vram, const void* command, int u, int v, int w, int h, unsigned tpage,
    unsigned clut, pc_ui_text_quad* quad)
{
    if (pc_ui_text != PC_UI_TEXT_FONT || !vram || !command || h != 10)
        return 0;
    unsigned now = call_now();
    for (int k = 1; k <= STATUS_STRINGS; k++) {
        status_string* s = &statuses[(status_next - k + STATUS_STRINGS) % STATUS_STRINGS];
        if (!s->count || now - s->stamp > CALL_AGE || (const char*)command < (const char*)s->glyph[0].command
            || (const char*)command > (const char*)s->glyph[s->count - 1].command)
            continue;
        for (int i = 0; i < s->count; i++) {
            if (s->glyph[i].command != command)
                continue;
            if (s->glyph[i].u != u || s->glyph[i].v != v || s->glyph[i].w != w || s->glyph[i].clut != clut
                || (s->tpage & 0x19f) != (tpage & 0x19f))
                return 0; /* another packet there since */
            if (s->generation != pc_ui_text_generation) {
                s->ok = status_layout(vram, s);
                s->generation = pc_ui_text_generation;
            }
            if (!s->ok || s->glyph[i].space)
                return 0;
            const text_image* im = &s->image[i];
            quad->image = STATUS_IMAGE_BASE + (int)(s - statuses) * STATUS_GLYPHS + i;
            quad->x = s->dx[i], quad->y = s->dy, quad->k = 1.0f;
            quad->w = im->w, quad->h = im->h, quad->row0 = 0, quad->row1 = im->h;
            ++status_glyphs_drawn;
            return 1;
        }
    }
    return 0;
}

/* A small-capital word from one of its sprites (`command`: the sprite's
 * command word in guest RAM). vs_battle_renderTextRawColor builds a call's
 * packets one after another: a texture page packet (E1), then a 5-word
 * sprite per character (10 rows of the info style's page, one CLUT, one
 * row), each linked to the next; the last links to what the ordering table
 * held. Anything else (another printer's glyphs, a packet reused since)
 * finds no word: drawn glyph by glyph, as before. */
#define GUEST_RAM 0x80000000u
#define GUEST_RAM_END 0x80200000u
static int in_guest_ram(const uint32_t* p, int count)
{
    uintptr_t a = (uintptr_t)p;
    return a >= GUEST_RAM && a % 4 == 0 && a + (uintptr_t)count * 4 <= GUEST_RAM_END;
}
static const uint32_t* linked(const uint32_t* p) { return (const uint32_t*)(uintptr_t)(GUEST_RAM | (p[0] & 0xffffff)); }
static int small_caps_packet(const uint32_t* p, unsigned clut, int y)
{
    return in_guest_ram(p, 5) && p[0] >> 24 == 4 && (p[1] >> 24 & 0xfc) == 0x64 && p[3] >> 16 == clut
        && p[4] >> 16 == 10 && (p[4] & 0xffff) >= 1 && (p[4] & 0xffff) <= 15 && (int16_t)(p[2] >> 16) == y;
}
static int find_word(const void* command, unsigned clut, status_string* s)
{
    const uint32_t* p = (const uint32_t*)command - 1;
    if (!in_guest_ram(p, 5))
        return 0;
    int y = (int16_t)(p[2] >> 16), back = 0;
    if (!small_caps_packet(p, clut, y))
        return 0;
    while (back++ < STATUS_GLYPHS && small_caps_packet(p - 5, clut, y) && linked(p - 5) == p)
        p -= 5;
    const uint32_t* t = p - 2;
    if (!in_guest_ram(t, 2) || t[0] >> 24 != 1 || t[1] >> 24 != 0xe1 || linked(t) != p)
        return 0; /* not the start of a call */
    memset(s, 0, sizeof *s);
    s->style = PC_UI_TEXT_INFO;
    s->tpage = t[1] & 0x7ff;
    for (;;) {
        if (s->count == STATUS_GLYPHS)
            return 0; /* (longer than any word: glyph by glyph) */
        s->glyph[s->count].command = p + 1;
        s->glyph[s->count].x = (int16_t)(p[2] & 0xffff), s->glyph[s->count].y = y;
        s->glyph[s->count].u = (int)(p[3] & 255), s->glyph[s->count].v = (int)(p[3] >> 8 & 255);
        s->glyph[s->count].clut = clut, s->glyph[s->count].w = (int)(p[4] & 0xffff);
        ++s->count;
        if (linked(p) != p + 5 || !small_caps_packet(p + 5, clut, y))
            break;
        p += 5;
    }
    return 1;
}

static int same_word(const status_string* a, const status_string* b)
{
    if (a->count != b->count || (a->tpage & 0x19f) != (b->tpage & 0x19f))
        return 0;
    for (int i = 0; i < a->count; i++)
        if (a->glyph[i].command != b->glyph[i].command || a->glyph[i].x != b->glyph[i].x
            || a->glyph[i].y != b->glyph[i].y || a->glyph[i].u != b->glyph[i].u || a->glyph[i].v != b->glyph[i].v
            || a->glyph[i].w != b->glyph[i].w || a->glyph[i].clut != b->glyph[i].clut)
            return 0;
    return 1;
}

int pc_ui_text_word_quad(const uint16_t* vram, const void* command, int u, int v, int w, int h, unsigned tpage,
    unsigned clut, pc_ui_text_quad* quad)
{
    glyph_info g;
    if (pc_ui_text != PC_UI_TEXT_FONT || pc_ui_text_info_words < 0.5f || !vram || !command || h != 10
        || !identify(tpage, clut, u, v, u + w - 1, v + h - 1, &g) || g.style != PC_UI_TEXT_INFO || g.symbol)
        return 0;
    static status_string found;
    if (!find_word(command, clut, &found))
        return 0;
    found.tpage = (found.tpage & ~0x19fu) | (tpage & 0x19f);
    int i = 0;
    while (i < found.count && found.glyph[i].command != command)
        ++i;
    if (i == found.count || found.glyph[i].u != u || found.glyph[i].v != v || found.glyph[i].w != w)
        return 0;
    /* The same word laid out before (this frame's or an earlier one's
     * packets), else a new one. */
    status_string* s = NULL;
    for (int k = 0; k < WORDS && !s; k++)
        if (words[k].count && same_word(&words[k], &found))
            s = &words[k];
    if (!s) {
        s = &words[word_next];
        word_next = (word_next + 1) % WORDS;
        *s = found;
    }
    s->stamp = call_now();
    if (s->generation != pc_ui_text_generation) {
        s->ok = status_layout(vram, s);
        s->generation = pc_ui_text_generation;
    }
    if (!s->ok || s->glyph[i].space)
        return 0;
    const text_image* im = &s->image[i];
    quad->image = WORD_IMAGE_BASE + (int)(s - words) * STATUS_GLYPHS + i;
    quad->x = s->dx[i], quad->y = s->dy, quad->k = 1.0f;
    quad->w = im->w, quad->h = im->h, quad->row0 = 0, quad->row1 = im->h;
    ++word_glyphs_drawn;
    return 1;
}

/* The small-capital words drawn lately (RPC ui_text {lines}: tests): each
 * letter's laid-out ink across, the font's own (its advances and kerning
 * alone, as condensed) and glyph by glyph's, from the first sprite's place. */
static int words_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size, "\"word_glyphs\":%ld,\"words\":[", word_glyphs_drawn), first = 1;
    unsigned now = call_now();
    for (int k = 0; k < WORDS && used < size - 4096; k++) {
        const status_string* s = &words[k];
        if (!s->count || !s->ok || now - s->stamp > CALL_AGE)
            continue;
        used += snprintf(out + used, (size_t)(size - used),
            "%s{\"text\":\"%s\",\"x\":%d,\"y\":%d,\"vsync\":%u,\"extent\":[%.3f,%.3f,%.3f,%.3f],"
            "\"original\":[%.3f,%.3f,%.3f,%.3f],\"tracking\":%.4f,\"condensed\":%.4f,\"letters\":[",
            first ? "" : ",", s->text, s->glyph[0].x, s->glyph[0].y, s->stamp, s->extent[0], s->extent[1],
            s->extent[2], s->extent[3], s->original[0], s->original[1], s->original[2], s->original[3],
            s->tracking, s->condensed);
        int any = 0;
        for (int i = 0; i < s->count; i++) {
            if (s->glyph[i].space)
                continue;
            used += snprintf(out + used, (size_t)(size - used),
                "%s{\"ink\":[%.3f,%.3f],\"font\":[%.3f,%.3f],\"cell\":[%.3f,%.3f]}", any ? "," : "", s->ink_x[i][0],
                s->ink_x[i][1], s->font_x[i][0], s->font_x[i][1], s->cell_x[i][0], s->cell_x[i][1]);
            any = 1;
        }
        used += snprintf(out + used, (size_t)(size - used), "]}");
        first = 0;
    }
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "]");
    return used < size ? used : size - 1;
}

/* The status words laid out lately (RPC ui_text status: tests). */
static int status_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size, "\"status_glyphs\":%ld,\"status\":[", status_glyphs_drawn), first = 1;
    unsigned now = call_now();
    for (int k = 1; k <= STATUS_STRINGS && used < size - 256; k++) {
        const status_string* s = &statuses[(status_next - k + STATUS_STRINGS) % STATUS_STRINGS];
        if (!s->count || !s->ok || now - s->stamp > CALL_AGE)
            continue;
        used += snprintf(out + used, (size_t)(size - used),
            "%s{\"text\":\"%s\",\"x\":%d,\"y\":%d,\"vsync\":%u,\"extent\":[%.3f,%.3f,%.3f,%.3f],"
            "\"original\":[%.3f,%.3f,%.3f,%.3f],\"scale\":%.4f,\"typeset\":%u}",
            first ? "" : ",", s->text, s->glyph[0].x, s->glyph[0].y, s->stamp, s->extent[0], s->extent[1],
            s->extent[2], s->extent[3], s->original[0], s->original[1], s->original[2], s->original[3],
            s->image[0].s, s->typeset.set);
        first = 0;
    }
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "]");
    return used < size ? used : size - 1;
}

int pc_ui_text_lines_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size,
        "{\"layout\":\"%s\",\"bubble_align\":\"%s\",\"unexplained\":%d,\"lines\":[",
        pc_ui_text_layout_names[pc_ui_text_layout ? 1 : 0],
        pc_ui_text_bubble_align_names[pc_ui_text_bubble_align ? 1 : 0], report_unexplained);
    int line = -1;
    for (int i = 0; i < report_count && used < size - 256; i++) {
        const report_glyph* e = &report[i];
        float k = e->k;
        if (e->line != line) {
            used += snprintf(out + used, (size_t)(size - used),
                "%s{\"style\":\"%s\",\"top\":%.3f,\"k\":%.4f,\"base\":%.3f,\"glyphs\":[", line >= 0 ? "]}," : "",
                pc_ui_text_styles[e->image.style].name, e->top, k, e->top + e->image.base * k);
            line = e->line;
        } else
            out[used++] = ',';
        /* The laid-out ink, per half image row (sampled at 8 per texel across). */
        const glyph_field* f = field(e->image.style, e->image.drawn);
        used += snprintf(out + used, (size_t)(size - used),
            "{\"c\":%u,\"seg\":%d,\"str\":%u,\"x\":%.3f,\"advance\":%g,\"ink\":[%.3f,%.3f,%.3f,%.3f],\"pen\":%.3f,"
            "\"left\":%.3f,\"rows\":[",
            e->codepoint, e->segment, e->string, e->x, (double)e->advance, e->ink[0], e->ink[1], e->ink[2], e->ink[3], e->pen, e->quad_x);
        int first = 1;
        for (int row = 0; f && row < e->image.h * 2 && used < size - 128; row++) {
            float lo = 1e9f, hi = -1e9f;
            for (int x = 0; x < e->image.w * 8; x++)
                if (image_distance(&e->image, f, (x + 0.5f) / 8, (row + 0.5f) / 2) > 0) {
                    lo = fminf(lo, (float)x), hi = fmaxf(hi, (float)x + 1);
                }
            if (hi > lo) {
                used += snprintf(out + used, (size_t)(size - used), "%s[%.3f,%.3f,%.3f]", first ? "" : ",",
                    e->top + (row + 0.5f) / 2 * k, e->quad_x + lo / 8 * k, e->quad_x + hi / 8 * k);
                first = 0;
            }
        }
        used += snprintf(out + used, (size_t)(size - used), "]}");
    }
    if (used < size - 8)
        used += snprintf(out + used, (size_t)(size - used), "%s],", line >= 0 ? "]}" : "");
    if (used < size - 8)
        used += words_json(out + used, size - used);
    if (used < size - 2)
        used += snprintf(out + used, (size_t)(size - used), "}");
    return used < size ? used : size - 1;
}

void pc_ui_text_warm(double budget_ms)
{
    static unsigned generation;
    static int style, next; /* where the walk through the character sets stands */
    if (generation != pc_ui_text_generation)
        generation = pc_ui_text_generation, style = 0, next = 0;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (; style < PC_UI_TEXT_STYLES; style++, next = 0) {
        const pc_ui_text_style* st = &pc_ui_text_styles[style];
        int capitals = style == PC_UI_TEXT_INFO || style == PC_UI_TEXT_STATUS;
        int count = capitals ? 0x5b - 0x21 : style == PC_UI_TEXT_DIGITS ? 10 : 189;
        for (; next < count; next++) {
            unsigned cp = capitals ? 0x21u + (unsigned)next
                        : style == PC_UI_TEXT_DIGITS ? '0' + (unsigned)next
                                                     : charset[next];
            if (!cp)
                continue;
            field(style, st->small_caps ? upper(cp) : cp);
            clock_gettime(CLOCK_MONOTONIC, &t);
            if ((t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_nsec - t0.tv_nsec) / 1e6 >= budget_ms) {
                next++;
                return;
            }
        }
    }
}

/* ---- settings ----
 *
 * Each style's fields (ui_text_<style>_<field>) and the tunables over them
 * (ui_text_<name>) are settings: the settings file (re-read when it changes
 * on disk), RPC ui_text and the panel set them; saved where they differ
 * from the built-in values (STYLE_DEFAULTS, the tunables' defaults). */
enum {
    FIELD_FONT,
    FIELD_STRETCH,
    FIELD_SLANT,
    FIELD_EMBOLDEN,
    FIELD_ASPECT,
    FIELD_SMALL_CAPS,
    FIELD_OUTLINE,
    FIELD_SIZE,
    FIELD_STROKE,
    FIELD_TRACKING,
    FIELDS
};
static const struct {
    const char* name;
    size_t offset;
    int integer; /* a flag (else a float clamped to lo..hi) */
    float lo, hi;
} fields[FIELDS] = {
    { "font", 0, 0, 0, 0 },
    { "stretch", offsetof(pc_ui_text_style, stretch), 0, 0.25f, 4.0f },
    { "slant", offsetof(pc_ui_text_style, slant), 0, -1.0f, 1.0f },
    { "embolden", offsetof(pc_ui_text_style, embolden), 0, -0.25f, 0.25f },
    { "aspect", offsetof(pc_ui_text_style, aspect), 0, 0.0f, 1.0f },
    { "small_caps", offsetof(pc_ui_text_style, small_caps), 1, 0, 1 },
    { "outline", offsetof(pc_ui_text_style, outline), 1, 0, 1 },
    { "size", offsetof(pc_ui_text_style, size), 0, 0.25f, 4.0f },
    { "stroke", offsetof(pc_ui_text_style, stroke), 0, 0.0f, 4.0f },
    { "tracking", offsetof(pc_ui_text_style, tracking), 0, -0.5f, 1.0f },
};
#define TUNABLES 7
static const struct {
    const char* name;
    float* value;
    float builtin, lo, hi;
} tunables[TUNABLES] = {
    { "word_space", &pc_ui_text_word_space, WORD_SPACE_DEFAULT, 0.5f, 4.0f },
    { "edge_margin", &pc_ui_text_edge_margin, EDGE_MARGIN_DEFAULT, 0.0f, 2.0f },
    { "popup_scale", &text_scale, 1.0f, 0.25f, 4.0f },
    { "descender_lift", &pc_ui_text_descender_lift, DESCENDER_LIFT_DEFAULT, 0.0f, 1.0f },
    { "info_words", &pc_ui_text_info_words, INFO_WORDS_DEFAULT, 0.0f, 1.0f },
    { "info_slack", &pc_ui_text_info_slack, INFO_SLACK_DEFAULT, 0.0f, 4.0f },
    { "info_tracking", &pc_ui_text_info_tracking, INFO_TRACKING_DEFAULT, 0.0f, 3.0f },
};

/* "ui_text_<style>_<field>": its style and field. */
static int style_key(const char* key, int* style, int* which)
{
    if (strncmp(key, "ui_text_", 8))
        return 0;
    for (int i = 0; i < PC_UI_TEXT_STYLES; i++) {
        size_t n = strlen(pc_ui_text_styles[i].name);
        if (strncmp(key + 8, pc_ui_text_styles[i].name, n) || key[8 + n] != '_')
            continue;
        for (int k = 0; k < FIELDS; k++)
            if (!strcmp(key + 9 + n, fields[k].name))
                return *style = i, *which = k, 1;
    }
    return 0;
}
static int tunable_key(const char* key)
{
    for (int t = 0; t < TUNABLES && !strncmp(key, "ui_text_", 8); t++)
        if (!strcmp(key + 8, tunables[t].name))
            return t;
    return -1;
}
static float tunable_parse(int t, const char* value)
{
    float v = strtof(value, NULL);
    return isfinite(v) ? fminf(fmaxf(v, tunables[t].lo), tunables[t].hi) : tunables[t].builtin;
}
/* One field from a settings value (a path; 1 / 0, true / false, on / off; a
 * number, clamped): 1 when it was understood. */
static int field_set(pc_ui_text_style* st, int k, const char* value)
{
    if (k == FIELD_FONT) {
        char path[256] = "";
        if (sscanf(value, " %255[^\r\n]", path) != 1)
            return 0;
        snprintf(st->font, sizeof st->font, "%s", path);
        return 1;
    }
    void* at = (char*)st + fields[k].offset;
    if (fields[k].integer) {
        char word[16] = "";
        sscanf(value, "%15s", word);
        *(int*)at = !strcmp(word, "1") || !strcmp(word, "true") || !strcmp(word, "on");
        return 1;
    }
    char* end;
    float v = strtof(value, &end);
    if (end == value || !isfinite(v))
        return 0;
    *(float*)at = fminf(fmaxf(v, fields[k].lo), fields[k].hi);
    return 1;
}
static int field_equal(const pc_ui_text_style* a, const pc_ui_text_style* b, int k)
{
    if (k == FIELD_FONT)
        return !strcmp(a->font, b->font);
    return !memcmp((const char*)a + fields[k].offset, (const char*)b + fields[k].offset, fields[k].integer ? sizeof(int)
                                                                                                         : sizeof(float));
}

/* Plugins' typography (rood.hud.typography, lua_api_hud.cpp): a runtime layer over the
 * settings, never saved. A field overridden keeps its setting in override_base (the
 * settings file, RPC ui_text and the saved lines see that one); a clear puts it back. */
static unsigned overridden[PC_UI_TEXT_STYLES];
static pc_ui_text_style override_base[PC_UI_TEXT_STYLES];

int pc_ui_text_style_index(const char* name)
{
    for (int i = 0; name && i < PC_UI_TEXT_STYLES; i++)
        if (!strcmp(name, pc_ui_text_styles[i].name))
            return i;
    return -1;
}

/* The tunables (word_space, edge_margin, popup_scale, descender_lift) a plugin's typography overrides
 * too, through a style (the digits' descender lift is rood.classic's): the style that holds each (-1:
 * none) and the settings' value under it. */
static int tunable_holder[TUNABLES] = { -1, -1, -1, -1, -1, -1, -1 };
static float tunable_base[TUNABLES];
#define TUNABLE_OVERRIDDEN (1u << 31) /* pc_ui_text_overridden's bit for them */

int pc_ui_text_override(int style, const char* field, const char* value)
{
    if (style < 0 || style >= PC_UI_TEXT_STYLES || !field || !value)
        return 0;
    for (int t = 0; t < TUNABLES; t++) {
        if (strcmp(field, tunables[t].name))
            continue;
        if (tunable_holder[t] < 0)
            tunable_base[t] = *tunables[t].value;
        tunable_holder[t] = style;
        float v = tunable_parse(t, value);
        if (v != *tunables[t].value)
            *tunables[t].value = v, ++pc_ui_text_generation;
        return 1;
    }
    for (int k = 0; k < FIELDS; k++) {
        if (strcmp(field, fields[k].name))
            continue;
        pc_ui_text_style probe = pc_ui_text_styles[style];
        if (!field_set(&probe, k, value))
            return 0;
        if (!overridden[style])
            override_base[style] = pc_ui_text_styles[style];
        else if (!(overridden[style] & 1u << k)) /* (the base follows what the settings set meanwhile) */
            memcpy((char*)&override_base[style] + (k == FIELD_FONT ? offsetof(pc_ui_text_style, font) : fields[k].offset),
                (const char*)&pc_ui_text_styles[style]
                    + (k == FIELD_FONT ? offsetof(pc_ui_text_style, font) : fields[k].offset),
                k == FIELD_FONT ? sizeof probe.font : fields[k].integer ? sizeof(int) : sizeof(float));
        overridden[style] |= 1u << k;
        if (!field_equal(&probe, &pc_ui_text_styles[style], k)) {
            pc_ui_text_styles[style] = probe;
            ++pc_ui_text_generation;
        }
        return 1;
    }
    return 0;
}

void pc_ui_text_override_clear(int style)
{
    for (int t = 0; t < TUNABLES && style >= 0; t++)
        if (tunable_holder[t] == style) {
            tunable_holder[t] = -1;
            if (*tunables[t].value != tunable_base[t])
                *tunables[t].value = tunable_base[t], ++pc_ui_text_generation;
        }
    if (style < 0 || style >= PC_UI_TEXT_STYLES || !overridden[style])
        return;
    pc_ui_text_style* st = &pc_ui_text_styles[style];
    for (int k = 0; k < FIELDS; k++) {
        if (!(overridden[style] & 1u << k))
            continue;
        size_t at = k == FIELD_FONT ? offsetof(pc_ui_text_style, font) : fields[k].offset;
        memcpy((char*)st + at, (const char*)&override_base[style] + at,
            k == FIELD_FONT ? sizeof st->font : fields[k].integer ? sizeof(int) : sizeof(float));
    }
    overridden[style] = 0;
    ++pc_ui_text_generation;
}

unsigned pc_ui_text_overridden(int style)
{
    unsigned mask = style >= 0 && style < PC_UI_TEXT_STYLES ? overridden[style] : 0;
    for (int t = 0; t < TUNABLES; t++)
        if (tunable_holder[t] == style && style >= 0)
            mask |= TUNABLE_OVERRIDDEN;
    return mask;
}

/* A field's style as the settings have it (under any override). */
static const pc_ui_text_style* settings_style(int style, int k)
{
    return overridden[style] & 1u << k ? &override_base[style] : &pc_ui_text_styles[style];
}

int pc_ui_text_setting_value(const char* key, char* out, int size)
{
    int style, which, t;
    if (style_key(key, &style, &which)) {
        const pc_ui_text_style* st = settings_style(style, which);
        if (which == FIELD_FONT)
            snprintf(out, (size_t)size, "%s", st->font);
        else if (fields[which].integer)
            snprintf(out, (size_t)size, "%d", *(const int*)((const char*)st + fields[which].offset));
        else
            snprintf(out, (size_t)size, "%.9g", (double)*(const float*)((const char*)st + fields[which].offset));
        return 1;
    }
    if ((t = tunable_key(key)) >= 0) {
        snprintf(out, (size_t)size, "%.9g", (double)(tunable_holder[t] >= 0 ? tunable_base[t] : *tunables[t].value));
        return 1;
    }
    return 0;
}

int pc_ui_text_setting(const char* key, const char* value)
{
    if (!strcmp(key, "ui_text")) { /* original | font (or 0 / 1) */
        char name[32] = "";
        sscanf(value, "%31s", name);
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_text_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_text = k;
        return 1;
    }
    if (!strcmp(key, "ui_text_bubble_align")) { /* left | centre (or center, 0 / 1) */
        char name[32] = "";
        sscanf(value, "%31s", name);
        if (!strcmp(name, "center"))
            strcpy(name, "centre");
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_text_bubble_align_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_text_bubble_align = k;
        return 1;
    }
    if (!strcmp(key, "ui_text_overlay")) { /* auto | off (or 1 / 0) */
        char name[32] = "";
        sscanf(value, "%31s", name);
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_text_overlay_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_text_overlay = k;
        return 1;
    }
    if (!strcmp(key, "ui_text_layout")) { /* line | glyph (or 1 / 0) */
        char name[32] = "";
        sscanf(value, "%31s", name);
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_text_layout_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_text_layout = k;
        return 1;
    }
    int style, which, t;
    if (style_key(key, &style, &which)) {
        if (overridden[style] & 1u << which) { /* the setting under a plugin's override (shown when it goes) */
            field_set(&override_base[style], which, value);
            return 1;
        }
        pc_ui_text_style probe = pc_ui_text_styles[style];
        if (field_set(&probe, which, value) && !field_equal(&probe, &pc_ui_text_styles[style], which)) {
            pc_ui_text_styles[style] = probe;
            ++pc_ui_text_generation; /* (only on a change: the settings file is re-read whole) */
        }
        return 1;
    }
    if ((t = tunable_key(key)) >= 0) {
        float v = tunable_parse(t, value);
        if (tunable_holder[t] >= 0) /* the setting under a plugin's override (back when it goes) */
            tunable_base[t] = v;
        else if (v != *tunables[t].value)
            *tunables[t].value = v, ++pc_ui_text_generation;
        return 1;
    }
    return 0;
}

static void field_print(FILE* f, const pc_ui_text_style* st, int k)
{
    if (k == FIELD_FONT)
        fputs(st->font, f);
    else if (fields[k].integer)
        fputs(*(const int*)((const char*)st + fields[k].offset) ? "1" : "0", f);
    else
        fprintf(f, "%g", (double)*(const float*)((const char*)st + fields[k].offset));
}

void pc_ui_text_settings_write(FILE* f)
{
    fprintf(f, "ui_text %s\n", pc_ui_text_names[pc_ui_text ? 1 : 0]);
    fprintf(f, "ui_text_layout %s\n", pc_ui_text_layout_names[pc_ui_text_layout ? 1 : 0]);
    fprintf(f, "ui_text_bubble_align %s\n", pc_ui_text_bubble_align_names[pc_ui_text_bubble_align ? 1 : 0]);
    fprintf(f, "ui_text_overlay %s\n", pc_ui_text_overlay_names[pc_ui_text_overlay ? 1 : 0]);
    /* Only what differs from the built-in values. */
    for (int i = 0; i < PC_UI_TEXT_STYLES; i++)
        for (int k = 0; k < FIELDS; k++)
            if (!field_equal(settings_style(i, k), &defaults[i], k)) { /* (never a plugin's override) */
                fprintf(f, "ui_text_%s_%s ", pc_ui_text_styles[i].name, fields[k].name);
                field_print(f, settings_style(i, k), k);
                fputc('\n', f);
            }
    for (int t = 0; t < TUNABLES; t++) {
        float v = tunable_holder[t] >= 0 ? tunable_base[t] : *tunables[t].value; /* (never a plugin's override) */
        if (v != tunables[t].builtin)
            fprintf(f, "ui_text_%s %g\n", tunables[t].name, (double)v);
    }
}

void pc_ui_text_get_stats(pc_ui_text_stats* s) { *s = stats; }

int pc_ui_text_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size, "{\"ui_text\":\"%s\",\"layout\":\"%s\",\"bubble_align\":\"%s\","
                                           "\"regions\":%ld,\"labels\":%ld,\"failed\":%ld,\"fonts\":%d,\"glyphs\":%d,"
                                           "\"field_ms\":%.1f,\"styles\":[",
        pc_ui_text_names[pc_ui_text ? 1 : 0], pc_ui_text_layout_names[pc_ui_text_layout ? 1 : 0],
        pc_ui_text_bubble_align_names[pc_ui_text_bubble_align ? 1 : 0], stats.regions, stats.labels, stats.failed,
        stats.fonts, stats.glyphs,
        stats.field_ms);
    for (int i = 0; i < PC_UI_TEXT_STYLES && used < size; i++) {
        const pc_ui_text_style* st = &pc_ui_text_styles[i];
        const font_file* loaded = NULL;
        for (int k = 0; k < font_count; k++)
            if (!strcmp(fonts[k].path, st->font))
                loaded = &fonts[k];
        used += snprintf(out + used, (size_t)(size - used),
            "%s{\"name\":\"%s\",\"font\":\"%s\",\"loaded\":%s,\"stretch\":%g,\"slant\":%g,\"embolden\":%g,"
            "\"aspect\":%g,\"small_caps\":%d,\"outline\":%d,\"size\":%g,\"stroke\":%g,\"tracking\":%g}",
            i ? "," : "", st->name, st->font, !loaded ? "null" : loaded->ok ? "true" : "false", st->stretch, st->slant,
            st->embolden, st->aspect, st->small_caps, st->outline, st->size, st->stroke, st->tracking);
    }
    if (used < size) {
        used += snprintf(out + used, (size_t)(size - used), "],\"tunables\":{");
        for (int t = 0; t < TUNABLES && used < size; t++)
            used += snprintf(out + used, (size_t)(size - used), "%s\"%s\":%g", t ? "," : "", tunables[t].name,
                (double)*tunables[t].value);
    }
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "},");
    if (used < size)
        used += status_json(out + used, size - used);
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), ",\"word_glyphs\":%ld", word_glyphs_drawn);
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), ",\"labels_drawn\":[");
    for (int i = 0, first = 1; i < LABEL_COUNT && used < size; i++)
        if (labels_drawn[i >> 6] >> (i & 63) & 1) {
            used += snprintf(out + used, (size_t)(size - used), "%s\"%s\"", first ? "" : ",", labels[i].text);
            first = 0;
        }
    if (used < size) /* (signs: ASCII, none a quote or backslash) */
        used += snprintf(out + used, (size_t)(size - used),
            "],\"signs_drawn\":\"%s\",\"popups_drawn\":\"%s\",\"popup_glyphs\":%ld,\"scale\":%g,\"popups\":[",
            signs_drawn, popups_drawn, popup_glyphs, (double)pc_ui_text_scale());
    for (int i = 0; i < popup_digit_count && used < size; i++)
        used += snprintf(out + used, (size_t)(size - used), "%s[\"%c\",%d,%d]", i ? "," : "", popup_digits[i].digit,
            popup_digits[i].x, popup_digits[i].y);
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "]}");
    return used;
}
