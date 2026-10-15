/* Modern typography for the game's text (ui_text.c; README "2D / UI").
 *
 * ui_text original (default) keeps the PS1's glyphs. ui_text font draws the
 * game's own text with open-licensed fonts (pc/third_party/fonts, chosen
 * with pc/tools/font_match.py): the game still decides every character,
 * position, colour, brightness and timing; only the texels of a glyph's box
 * are replaced, from a signed distance field of the font's glyph placed on
 * the original glyph (its baseline, cap height and ink centre, measured from
 * the game's VRAM), as distance texels (below) coloured through the same
 * CLUT ramp the original glyph uses and drawn anti-aliased at whatever
 * resolution they land on (dialogue and menu lines set again with the font's
 * own spacing: ui_text_layout; at the window's resolution: ui_text_overlay).
 * It goes through the 2D atlas of ui_upscale.c (any internal resolution). */
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

/* String-level layout (ui_text_layout; README "2D / UI"). line: the glyphs
 * of each string a game printer sets (talk bubbles, menus, the information
 * box, the memory card screens) are set with the font's own advances and
 * kerning from where the game starts it, never past the game's own glyph
 * positions by more than a pixel (tightened, never overlapping, when the
 * font is wider). glyph (default for now): each glyph in its original cell
 * at the game's advance; also any glyph no printer reported in line layout.
 * Either way each glyph is drawn whole: descenders below the 12-pixel cell. */
enum { PC_UI_TEXT_LAYOUT_GLYPH = 0, PC_UI_TEXT_LAYOUT_LINE = 1 };
extern int pc_ui_text_layout;
extern const char* const pc_ui_text_layout_names[2];
/* ui_text_bubble_align (line layout, talk bubbles only; menus and the
 * information box stay left): left, each line starts where the game starts
 * it; centre (default), each line centred within the original line's extent
 * (the whole line, from the game's text box, while it types out). */
enum { PC_UI_TEXT_ALIGN_LEFT = 0, PC_UI_TEXT_ALIGN_CENTRE = 1 };
extern int pc_ui_text_bubble_align;
extern const char* const pc_ui_text_bubble_align_names[2];
/* ui_text_overlay (gpu_hw.c "text at the window's resolution"): auto
 * (default), text and icons from fonts drawn at the window's resolution over
 * the presented picture while the window is finer than the internal
 * resolution (and into the picture where anything comes over them); off,
 * into the picture at the internal resolution. */
enum { PC_UI_TEXT_OVERLAY_OFF = 0, PC_UI_TEXT_OVERLAY_AUTO = 1 };
extern int pc_ui_text_overlay;
extern const char* const pc_ui_text_overlay_names[2];
/* Whether dialogue and menu glyphs are laid out now (ui_text font, any
 * internal resolution, either ui_text_layout). */
int pc_ui_text_layout_on(int scale);

/* gpu_hw.c hands the layout every 2D sprite that is a whole-width piece of a
 * dialogue or menu glyph (a 12 x 12 cell, or one of the 12 x 1 strips menu
 * rows are drawn in), and every axis-aligned quad drawing a whole cell
 * scaled (a talk bubble's text while it zooms), while it is on, buffered
 * until something else is drawn: a run of pieces drawn one after another,
 * laid out together. */
int pc_ui_text_piece(const uint16_t* vram, unsigned tpage, unsigned clut, int u, int v, int w, int h);
typedef struct {
    float x, y;        /* the piece's top-left on screen (drawing offset applied) */
    float k;           /* screen pixels per texel (1 for sprites) */
    int u, v, h;       /* its first texel, its height in texels */
    unsigned tpage, clut;
    int clip[4];       /* its drawing area */
    unsigned string;   /* the printer's string it belongs to (pc_text_glyph; 0: unknown, drawn glyph by glyph) */
    float advance;     /* the printer's advance after it (texels) */
} pc_ui_text_piece_info;

/* The game's text printers (src/, ROOD_PC: vs_battle_printVariableWidthFontChar,
 * vs_mainMenu_renderRowLabel, MENU7's and TITLE's _printCharacter; fixed_font.c)
 * report each character they set: glyph >= 0 with its sprite packets'
 * command words (count: 1, or 12 one-row strips), glyph -1 for a space (no
 * packet); x, y the pen and next_x where it moves (16.16 pixels), advance in
 * texels. Characters set one after another on one line, each where the
 * previous one left the pen (or a few spaces on), are one string: line layout
 * sets a string's glyphs together and only glyphs a printer reported. */
void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance);
/* The string of the sprite whose command word is at `command` (gpu_hw.c, as
 * a piece is buffered): info->string and info->advance; 0 when no printer
 * reported it. */
int pc_ui_text_piece_call(const void* command, pc_ui_text_piece_info* info);
typedef struct {
    int image;      /* the glyph's image (pc_ui_text_image_*), -1: draw the original piece */
    float x, y;     /* the quad's top-left on screen (PS1 pixels) */
    float k;        /* screen pixels per image texel */
    int w, h;       /* its image's width and height in texels */
    int row0, row1; /* the image rows it draws: [row0, row1), screen y = y + (row - row0) * k */
} pc_ui_text_quad;
/* Lay out `count` pieces (in drawing order) as `mode` (PC_UI_TEXT_LAYOUT_*:
 * both are drawn, the option picks one when the frame is drawn); out[i] is
 * piece i's quad (unrounded: drawn anti-aliased). `scale`: internal resolution. */
void pc_ui_text_layout_run(const uint16_t* vram, const pc_ui_text_piece_info* pieces, int count, int scale, int mode,
    pc_ui_text_quad* out);
/* A laid-out glyph's image (valid until the next pc_ui_text_layout_run):
 * its content key and size in texels, and its texels at `factor` (PS1
 * colours, 0 transparent). */
int pc_ui_text_image_key(int image, uint64_t* key, int* w, int* h);
int pc_ui_text_image_render(int image, int factor, uint32_t* out);
/* JSON (RPC ui_text {lines: true}): the lines laid out in the newest frame
 * that drew text: per glyph its character, the original glyph's position,
 * advance and ink, the laid-out pen and ink (screen PS1 pixels; ink per
 * image row as [row, x0, x1]), and per line its baseline. */
int pc_ui_text_lines_json(char* out, int size);

/* Make the distance fields of the characters the styles can show, at most
 * `budget_ms` of CPU per call (once per presented frame while ui_text is
 * font), so text appearing later does not stall a frame making them. */
void pc_ui_text_warm(double budget_ms);

/* A new presented frame (ui_upscale.c): the next layout starts the lines report afresh. */
void pc_ui_text_frame(void);

/* Settings lines (project_settings.cpp): 1 when `key` was one of ours. */
int pc_ui_text_setting(const char* key, const char* value);
void pc_ui_text_settings_write(FILE* f);
/* JSON object (RPC ui_text): option, styles, fonts loaded, glyphs cached,
 * regions drawn. */
int pc_ui_text_json(char* out, int size);
/* The json's signs_drawn starts again (RPC ui_text forget_signs: tests). */
void pc_ui_text_forget_signs(void);
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

/* Distance texels: what ui_text.c and button_prompts.c put in the 2D atlas
 * for text and icons (PS1 colours, 0..0xffff, are MMPX's), so the hardware
 * path draws them anti-aliased at whatever resolution they land on (the
 * shaders in gpu_hw.c and pc/shaders/vulkan/hw_frag.glsl decode them as
 * pc_ui_dist_colour does):
 *   bit 31     set: a distance texel
 *   bits 13-0  signed distance to the edge, PS1 pixels * 256 (positive inside)
 *   bit 30     direct colour: bits 28-14 a 5:5:5 colour, bit 29 its STP bit
 *   else       bits 29-28 PC_UI_DIST_*, bit 27 a label's outline, bits 26-23
 *              the CLUT index at full coverage (hi), bits 22-19 at the edge
 *              (lo; a label's outline index)
 * RAMP: coverage 0..1 picks CLUT index lo..hi (the original glyph's ramp),
 * the coverage its alpha. OUTLINED: that ramp inside a one-pixel outline of
 * CLUT index 1 (the outline's outer edge the alpha). LABEL: outline colour
 * lo (when bit 27) blended into body colour hi by coverage. */
enum { PC_UI_DIST_RAMP = 0, PC_UI_DIST_OUTLINED = 1, PC_UI_DIST_LABEL = 2 };
#define PC_UI_DIST_FAR (-32.0f) /* PS1 pixels: the farthest distance kept */
uint32_t pc_ui_dist_texel(float d, int mode, int lo, int hi, int outlined);
uint32_t pc_ui_dist_direct(float d, uint16_t colour);
static inline float pc_ui_dist_of(uint32_t t)
{
    return (t >> 31) ? (float)((int32_t)(t << 18) >> 18) / 256.0f : PC_UI_DIST_FAR;
}
/* The PS1 colour (0: none) and coverage of distance texel `t` drawn with
 * edges `width` PS1 pixels wide, its distance `d` (sampled between texels;
 * pc_ui_dist_of for the texel's own), through CLUT `lut`. */
uint16_t pc_ui_dist_colour(uint32_t t, float d, const uint16_t* lut, float width, float* coverage);
/* Distance texels into PS1 colours in place (tests: RPC ui_text render,
 * button_prompts masks): as drawn at `factor` texels a PS1 pixel, those
 * covered more than `least` (< 0: as the glyphs were before they were drawn
 * anti-aliased, a fiftieth, a quarter for icons). */
void pc_ui_text_resolve(const uint16_t* vram, unsigned clut, uint32_t* texels, int count, int factor, float least);

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
