/* Modern typography for the game's text (ui_text.c; README "2D / UI").
 *
 * ui_text original (default) keeps the PS1's glyphs. ui_text font draws the
 * game's own text with open-licensed fonts (pc/third_party/fonts, chosen
 * with pc/tools/font_match.py): the game still decides every character,
 * position, colour, brightness and timing; only the texels of a glyph's box
 * are replaced, at the internal resolution, from a signed distance field of
 * the font's glyph placed on the original glyph (its baseline, cap height
 * and ink centre, measured from the game's VRAM) and mapped through the
 * same CLUT ramp the original glyph uses. It goes through the 2D atlas of
 * ui_upscale.c (internal resolution 2x and up). */
#ifndef ROOD_UI_TEXT_H
#define ROOD_UI_TEXT_H
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_TEXT_ORIGINAL = 0, PC_UI_TEXT_FONT = 1 };
extern int pc_ui_text; /* PC_UI_TEXT_*; original keeps the PS1 glyphs */
extern const char* const pc_ui_text_names[2];

/* Text styles and their font settings (settings ui_text_<style>_font,
 * _stretch, _slant, _embolden, _aspect; RPC ui_text). Fixed labels baked
 * into the UI's images (HP, MP, INFORMATION, ...) are drawn with the info
 * style's font. */
enum { PC_UI_TEXT_DIALOGUE, PC_UI_TEXT_MENU, PC_UI_TEXT_INFO, PC_UI_TEXT_DIGITS, PC_UI_TEXT_STYLES };
typedef struct {
    const char* name;  /* dialogue, menu, info, digits */
    char font[256];    /* TrueType file (relative to the repository root) */
    float stretch;     /* horizontal scale of the glyphs */
    float slant;       /* added shear (x per y, original pixels) */
    float embolden;    /* distance-field offset in em (bolder > 0) */
    float aspect;      /* digits: least horizontal scale of the font's own proportions (0: any); a size whose
                        * widest digit needs more condensing than this is drawn smaller instead */
    int small_caps;    /* lower case drawn as small capitals (the upper-case glyph at the original's height) */
    int outline;       /* the original has a one-pixel outline (CLUT index 1) around a body ramp */
} pc_ui_text_style;
extern pc_ui_text_style pc_ui_text_styles[PC_UI_TEXT_STYLES];
/* Bumped when a style changes (the atlas keeps glyphs per generation). */
extern unsigned pc_ui_text_generation;

/* Whether the 2D region [u0, u1] x [v0, v1] of page `tpage` / CLUT `clut`
 * is (part of) one glyph the font draws; then *key identifies the glyph
 * for the cache. */
int pc_ui_text_classify(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    uint64_t* key);
/* The region's texels at `factor` per texel, PS1 colours (0 transparent),
 * row by row ((u1 - u0 + 1) * factor wide); 0 when it cannot be drawn. */
int pc_ui_text_render(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, uint32_t* out);

/* Make the distance fields of the characters the styles can show, at most
 * `budget_ms` of CPU per call (once per presented frame while ui_text is
 * font), so text appearing later does not stall a frame making them. */
void pc_ui_text_warm(double budget_ms);

/* Settings lines (project_settings.cpp): 1 when `key` was one of ours. */
int pc_ui_text_setting(const char* key, const char* value);
void pc_ui_text_settings_write(FILE* f);
/* JSON object (RPC ui_text): option, styles, fonts loaded, glyphs cached,
 * regions drawn. */
int pc_ui_text_json(char* out, int size);
typedef struct {
    long regions, failed; /* regions drawn from a font / that fell back to the original */
    long labels;          /* of those regions, fixed labels (text baked into images) */
    int fonts, glyphs;    /* fonts loaded, glyph distance fields made */
    double field_ms;      /* CPU time making them */
} pc_ui_text_stats;
void pc_ui_text_get_stats(pc_ui_text_stats* s);

/* A game string (the vsString encoding, 0xE7-terminated, at most max_bytes
 * read) as UTF-8 into out (always terminated): charset codes, 0x8F space,
 * 0xFA n spacing and 0xE8 newline as one space (blanks collapsed, none
 * leading or trailing); other control bytes and pictographs are skipped.
 * Returns the UTF-8 length. For the modern menus (pc/src/ui). */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

/* Icons from any TrueType file (button_prompts.c): the glyph's signed
 * distance field, made at first use and kept (NULL when the font or the
 * glyph is missing: *missing 1). Its box (x0, bottom, x1, top) and
 * distances are field pixels (positive inside; y up from the baseline). */
typedef struct pc_ui_icon pc_ui_icon;
const pc_ui_icon* pc_ui_icon_glyph(const char* font_path, unsigned codepoint, int* missing);
void pc_ui_icon_box(const pc_ui_icon* icon, float box[4]);
float pc_ui_icon_distance(const pc_ui_icon* icon, float x, float y);

#ifdef __cplusplus
}
#endif
#endif
