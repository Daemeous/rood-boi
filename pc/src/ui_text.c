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
 *             7 x 12 boxes from row 0
 *
 * and its texels are drawn again at the atlas's resolution from the
 * style's font: a signed distance field per glyph (stb_truetype, made at
 * first use and kept), placed on the original glyph box with the
 * original's baseline and cap height (measured on its H, small capitals on
 * its x) and centred on the original glyph's ink, then mapped through the
 * original glyph's own CLUT ramp (coverage 0..1 -> CLUT index 1..max, or
 * 2..max inside a one-pixel outline of index 1), so colours, brightness,
 * gradients, translucency and the colour key are the game's. Line breaks and
 * timing never change. With ui_text_layout glyph (the default for now) the
 * positions and advances do not either (each glyph in its original cell,
 * descenders below it); with line dialogue and menu text is set string by
 * string with the font's own spacing ("string-level layout" below). Characters a font lacks, and the
 * pictographs (button symbols, arrows, notes), stay original.
 *
 * Words the game draws as pictures (HP, MP, COMMAND, MENU, INFORMATION,
 * BATTLE MODE, ...: ui_labels.h, by page, box and hash) are set in the info
 * style's font in the word's original box and colours ("fixed labels"
 * below). */
#define _POSIX_C_SOURCE 200809L
#include "ui_text.h"
#include "ui_labels.h"
#include <math.h>
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
            0.0f, 1, 0 },                                                                                        \
        { "menu", "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf", 1.18f, 0.0f, -0.02f, 0.0f, 0, 0 }, \
        { "info", "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf", 1.26f, 0.0f, 0.0f, 0.0f, 0, 1 },     \
        { "digits", "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf", 1.07f, 0.0f, 0.02f, DIGITS_ASPECT, 0, 1 }, \
    }
pc_ui_text_style pc_ui_text_styles[PC_UI_TEXT_STYLES] = STYLE_DEFAULTS;
static const pc_ui_text_style defaults[PC_UI_TEXT_STYLES] = STYLE_DEFAULTS;

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

/* ---- fonts and glyph distance fields ---- */

#define SDF_EM 64   /* distance field pixels per em (cap height ~45: upsampled smoothly at 8x) */
#define SDF_PAD 16  /* field reaches this many of its pixels outside the outline (> one original pixel) */
#define SDF_SCALE (127.0f / SDF_PAD)

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
        for (int code = 0x21; code <= 0x5a; code++) {
            int u, v, w;
            if (small_find(code, &u, &v, &w) && u == u0 && v == v0 && u1 == u0 + w - 1) {
                g->style = PC_UI_TEXT_INFO;
                g->codepoint = (unsigned)code;
                g->box_u = u, g->box_v = v, g->box_w = w, g->box_h = 10;
                g->advance = w;
                if (!small_find('H', &g->ref_u, &g->ref_v, &g->ref_w))
                    return 0;
                g->ref_h = 10;
                g->threshold = 4;
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
        else if (u0 >= 64 && u0 < 144 && (u0 - 64) % 8 == 0 && u1 == u0 + 6 && v1 == 11)
            digit = (u0 - 64) / 8, w = 7, h = 12, ref = 64;
        if (digit < 0)
            return 0;
        g->style = PC_UI_TEXT_DIGITS;
        g->codepoint = digit == 10 ? '/' : '0' + (unsigned)digit;
        g->box_u = u0, g->box_v = 0, g->box_w = w, g->box_h = h;
        g->advance = w;
        g->ref_u = ref, g->ref_v = 0, g->ref_w = w, g->ref_h = h;
        g->threshold = 3; /* coverage 0.2 of 9 */
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
void pc_ui_text_forget_signs(void) { memset(signs_drawn, 0, sizeof signs_drawn); }

static int texel4(const uint16_t* vram, int page_x, int page_y, int u, int v)
{
    return (vram[((page_y + v) & 511) * 1024 + ((page_x + (u >> 2)) & 1023)] >> ((u & 3) * 4)) & 15;
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

static int label_layout_make(const uint16_t* vram, unsigned clut, const pc_ui_label* l, label_layout* o)
{
    const pc_ui_text_style* st = &pc_ui_text_styles[PC_UI_TEXT_INFO];
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
    /* The word in the font: pen positions with the font's advances and
     * kerning, its ink extent and height above the baseline. */
    float pen = 0, x0 = 1e9f, x1 = -1e9f, height = 0;
    int previous = 0;
    for (const char* c = l->text; *c; c++) {
        int index = stbtt_FindGlyphIndex(&f->info, (unsigned char)*c);
        if (previous && index)
            pen += stbtt_GetGlyphKernAdvance(&f->info, previous, index) * f->scale;
        if (*c != ' ') {
            const glyph_field* g = field(PC_UI_TEXT_INFO, (unsigned char)*c);
            if (!g || o->count == LABEL_CHARS)
                return 0;
            o->glyph[o->count] = g;
            o->pen[o->count++] = pen;
            x0 = fminf(x0, pen + g->x0), x1 = fmaxf(x1, pen + g->x1), height = fmaxf(height, g->top);
        }
        int advance, bearing;
        stbtt_GetGlyphHMetrics(&f->info, index, &advance, &bearing);
        pen += advance * f->scale;
        previous = index;
    }
    if (!o->count || height <= 0 || x1 <= x0)
        return 0;
    /* Its height is the original's (cap top to baseline, or the tallest
     * letter's top), its width the font's own, condensed only where the
     * word would leave the original's body. */
    o->s = (o->base - top) / height;
    o->sx = o->s * st->stretch;
    float room = (float)(bx1 + 1 - bx0);
    if ((x1 - x0) * o->sx > room)
        o->sx = room / (x1 - x0);
    o->cx_t = (bx0 + bx1 + 1) * 0.5f, o->cx_s = (x0 + x1) * 0.5f;
    return 1;
}

static int render_labels(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, uint32_t* out)
{
    int found[MAX_REGION_LABELS];
    int n = find_labels(vram, tpage, u0, v0, u1, v1, found);
    static label_layout layouts[MAX_REGION_LABELS];
    for (int k = 0; k < n; k++)
        if (!label_layout_make(vram, clut, &labels[found[k]], &layouts[k]))
            return 0;
    if (n <= 0)
        return 0;
    const pc_ui_text_style* st = &pc_ui_text_styles[PC_UI_TEXT_INFO];
    float bold = st->embolden * SDF_EM, inv = 1.0f / factor;
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
                    float fx = o->cx_s + (xo - o->cx_t) / o->sx, fy = (o->base - yo) / o->s;
                    for (int i = 0; i < o->count; i++) {
                        const glyph_field* g = o->glyph[i];
                        float gx = fx - o->pen[i];
                        if (gx < g->x0 - SDF_PAD || gx > g->x1 + SDF_PAD)
                            continue;
                        d = fmaxf(d, distance_at(g, gx, fy));
                    }
                    d = (d + bold) * fminf(o->s, o->sx); /* original pixels */
                }
                if (d <= best)
                    continue;
                best = d;
                texel = pc_ui_dist_texel(d, PC_UI_DIST_LABEL, o->outline_index, o->body_index, o->outline != 0);
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
        if (n > 0 && field(PC_UI_TEXT_INFO, 'H')) {
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
    *key = (uint64_t)pc_ui_text_generation << 40 ^ (uint64_t)g.style << 36 ^ (uint64_t)g.codepoint << 8;
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
    float room = st->outline ? g.box_w - 2.0f : fmaxf((float)(box[2] - box[0]), (float)g.advance) + 0.5f;
    if (g.style == PC_UI_TEXT_DIGITS && !bullet && st->aspect > 0) /* (a sign: the small digits' scale) */
        s = digit_scale(g.style, s, st->aspect, g.symbol ? g.ref_w + 0.5f : room);
    float sx = s * st->stretch;
    if ((f->x1 - f->x0) * sx > room)
        sx = room / (f->x1 - f->x0);
    /* Descenders squeezed into the box (the PS1 faces have none). */
    float below = g.box_h - base - (st->outline ? 1.0f : 0.0f) - 0.1f, sd = s;
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
    if (st->outline) { /* the ink keeps a pixel from the box's edges (the outline; neighbours overlap them) */
        float half = (f->x1 - f->x0) * sx * 0.5f;
        cx_t = fminf(fmaxf(cx_t, 1.0f + half), g.box_w - 1.0f - half);
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
    int mode = st->outline ? PC_UI_DIST_OUTLINED : PC_UI_DIST_RAMP;
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
            out[y * w + x] = pc_ui_dist_texel(d, mode, lo, hi, mode == PC_UI_DIST_LABEL);
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
 *   space (widened, WORD_SPACE) for every space the game left (a gap of 3
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
/* The font's space widened: the game's words stand a whole letter apart, and
 * at these sizes (8-pixel capitals) a text face's own space reads as none. */
#define WORD_SPACE 1.5f

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

void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance)
{
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
                    pen += spaces * w * m->font->scale * fmaxf(q->sx, m->sx) * WORD_SPACE * k;
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

int pc_ui_text_image_key(int image, uint64_t* key, int* w, int* h)
{
    if (image < 0 || image >= image_count)
        return 0;
    *key = image_hash(&images[image]);
    *w = images[image].w, *h = images[image].h;
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
    for (int y = 0; y < rows; y++) {
        float yo = (y + 0.5f) * inv;
        for (int x = 0; x < w; x++)
            out[y * w + x] = pc_ui_dist_texel(image_distance(im, f, (x + 0.5f) * inv, yo), PC_UI_DIST_RAMP, im->lo,
                im->hi, 0);
    }
    return 1;
}

int pc_ui_text_image_render(int image, int factor, uint32_t* out)
{
    if (image < 0 || image >= image_count || !draw_image(&images[image], factor, out)) {
        ++stats.failed;
        return 0;
    }
    ++stats.regions;
    return 1;
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
        used += snprintf(out + used, (size_t)(size - used), "%s]}", line >= 0 ? "]}" : "");
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
        int count = style == PC_UI_TEXT_INFO ? 0x5b - 0x21 : style == PC_UI_TEXT_DIGITS ? 10 : 189;
        for (; next < count; next++) {
            unsigned cp = style == PC_UI_TEXT_INFO ? 0x21u + (unsigned)next
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

/* ---- settings ---- */

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
    if (strncmp(key, "ui_text_", 8))
        return 0;
    for (int i = 0; i < PC_UI_TEXT_STYLES; i++) {
        pc_ui_text_style* st = &pc_ui_text_styles[i];
        size_t n = strlen(st->name);
        if (strncmp(key + 8, st->name, n) || key[8 + n] != '_')
            continue;
        const char* field_name = key + 9 + n;
        if (!strcmp(field_name, "font")) {
            char path[256] = "";
            if (sscanf(value, " %255[^\r\n]", path) == 1)
                snprintf(st->font, sizeof st->font, "%s", path);
        } else if (!strcmp(field_name, "stretch"))
            st->stretch = strtof(value, NULL);
        else if (!strcmp(field_name, "slant"))
            st->slant = strtof(value, NULL);
        else if (!strcmp(field_name, "embolden"))
            st->embolden = strtof(value, NULL);
        else if (!strcmp(field_name, "aspect"))
            st->aspect = fminf(fmaxf(strtof(value, NULL), 0.0f), 1.0f);
        else
            return 0;
        if (st->stretch < 0.25f || st->stretch > 4.0f)
            st->stretch = 1.0f;
        ++pc_ui_text_generation;
        return 1;
    }
    return 0;
}

void pc_ui_text_settings_write(FILE* f)
{
    fprintf(f, "ui_text %s\n", pc_ui_text_names[pc_ui_text ? 1 : 0]);
    fprintf(f, "ui_text_layout %s\n", pc_ui_text_layout_names[pc_ui_text_layout ? 1 : 0]);
    fprintf(f, "ui_text_bubble_align %s\n", pc_ui_text_bubble_align_names[pc_ui_text_bubble_align ? 1 : 0]);
    fprintf(f, "ui_text_overlay %s\n", pc_ui_text_overlay_names[pc_ui_text_overlay ? 1 : 0]);
    /* Only what differs from the built-in choice. */
    for (int i = 0; i < PC_UI_TEXT_STYLES; i++) {
        const pc_ui_text_style *st = &pc_ui_text_styles[i], *d = &defaults[i];
        if (strcmp(st->font, d->font))
            fprintf(f, "ui_text_%s_font %s\n", st->name, st->font);
        if (st->stretch != d->stretch)
            fprintf(f, "ui_text_%s_stretch %g\n", st->name, st->stretch);
        if (st->slant != d->slant)
            fprintf(f, "ui_text_%s_slant %g\n", st->name, st->slant);
        if (st->embolden != d->embolden)
            fprintf(f, "ui_text_%s_embolden %g\n", st->name, st->embolden);
        if (st->aspect != d->aspect)
            fprintf(f, "ui_text_%s_aspect %g\n", st->name, st->aspect);
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
            "\"aspect\":%g,\"small_caps\":%d,\"outline\":%d}",
            i ? "," : "", st->name, st->font, !loaded ? "null" : loaded->ok ? "true" : "false", st->stretch, st->slant,
            st->embolden, st->aspect, st->small_caps, st->outline);
    }
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "],\"labels_drawn\":[");
    for (int i = 0, first = 1; i < LABEL_COUNT && used < size; i++)
        if (labels_drawn[i >> 6] >> (i & 63) & 1) {
            used += snprintf(out + used, (size_t)(size - used), "%s\"%s\"", first ? "" : ",", labels[i].text);
            first = 0;
        }
    if (used < size) /* (signs: ASCII, none a quote or backslash) */
        used += snprintf(out + used, (size_t)(size - used), "],\"signs_drawn\":\"%s\"}", signs_drawn);
    return used;
}
