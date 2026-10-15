/* Modern menus, the Quick manual: the page files and the original's rules
 * as pure functions (no game headers; linked into the game and
 * ui_shell_probe). The original is MENUE (src/MENU/MENUE.PRG/494.c); the
 * files are the disc's SMALL/HELPnn.HF0 (text, illustrations' placement,
 * rules) and .HF1 (the illustrations' 8 x 16 pixel blocks and 16-colour CLUT
 * lines), read at run time and never written anywhere.
 *
 * HF0: u32 sprite block offset (from byte 16), u32 rule block offset (from
 * the sprite block), u32 rule bytes (> 0: rules present), u32 0; at byte 16
 * the text: u16 n, u16 off[n] (halfwords from byte 16), each line a game
 * string ended by 0xE7. Sprite block: u16 n, s16 off[n] (halfwords from the
 * block); a sprite is an animation (8 shorts), x, y, w, h, blocks, colours
 * (256: 8bpp, 16: 4bpp), the packed CLUT (x4 | y12) and one block id per
 * 16-high band and 8 (16) wide column. Rule block: u16 n, then 28 bytes
 * each: an animation, x0, y0, x1, y1, r, g, b, pad. Content coordinates:
 * x 0..300 from the text origin, line i at y = 13 i (494.c _drawSprite,
 * _drawContentLines).
 *
 * HF1: u32 blocks, u32 CLUT lines, 128-byte blocks (8 x 16, 8bpp), 32-byte
 * CLUT lines (16 BGR555 colours). */
#ifndef PC_UI_MANUAL_PARSE_H
#define PC_UI_MANUAL_PARSE_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_MANUAL_TOPICS = 14, PC_UI_MANUAL_PAGE_LINES = 10, PC_UI_MANUAL_LINE_PX = 13,
       PC_UI_MANUAL_WIDTH_PX = 300 };
/* A run's kind: a word of the charset, a face button pictograph (glyph 0 triangle, 1 square, 2 circle,
 * 3 cross), the star, an arrow (glyph 0 left, 1 right, 2 up, 3 down: the text's relation arrows). */
enum { PC_UI_MANUAL_WORD, PC_UI_MANUAL_GLYPH, PC_UI_MANUAL_STAR, PC_UI_MANUAL_ARROW };
/* How a run follows the one before it on its line, by the original's pixels between them: touching it (a
 * pictograph after a word, a colour change inside a word), a space's worth (up to JOIN_PX: one space, or a
 * small FA advance used as one), or apart (the line's first run, wider gaps: a column the original aligns,
 * or room left for an illustration; the reader anchors the run at its original x). */
enum { PC_UI_MANUAL_GAP_NONE, PC_UI_MANUAL_GAP_SPACE, PC_UI_MANUAL_GAP_APART };
enum { PC_UI_MANUAL_JOIN_PX = 9 };

typedef struct {
    unsigned char kind;   /* PC_UI_MANUAL_* */
    unsigned char colour; /* FB 0..3 in force */
    unsigned short x, w;  /* the original's place and advance (pixels, its proportional font) */
    unsigned off, len;    /* the word's bytes in the HF0 buffer (line starts reach ~128 KiB past 16) */
    unsigned char glyph;  /* GLYPH / ARROW: which */
    unsigned char gap;    /* PC_UI_MANUAL_GAP_* */
} pc_ui_manual_run;
/* AnimationState_t as on disc (8 shorts, the last one padding). */
typedef struct {
    short enabled, mask, duration, repeat, cur, timer, active, pad;
} pc_ui_manual_anim;
typedef struct {
    pc_ui_manual_anim a;
    short x, y, w, h;
    int tex; /* index into the topic's rgba images (identical sprites share one) */
} pc_ui_manual_sprite;
typedef struct {
    pc_ui_manual_anim a;
    short x0, y0, x1, y1;
    unsigned char r, g, b;
} pc_ui_manual_rule;
typedef struct {
    int ok;          /* the header and the text table read */
    int lines;       /* the text's line count (HF0's n) */
    int line_count;  /* the last scroll position (pc_ui_manual_line_count) */
    int pages;       /* line_count / 10 + 1 */
    int nruns, nsprites, nrules, ntex;
    int unknown;     /* codes and entries skipped (bad offsets, control codes the reader does not draw) */
    int* line_first; /* lines + 1: line i's runs are [line_first[i], line_first[i + 1]) */
    pc_ui_manual_run* runs;
    pc_ui_manual_sprite* sprites;
    pc_ui_manual_rule* rules;
    unsigned char** rgba; /* ntex images, each w * h * 4 (the sprite's w, h; 0x0000 transparent) */
    short* tex_w;
    short* tex_h;
    signed char* tex_art; /* ntex: the pad button an image shows (PC_UI_MANUAL_ART_*), -1 none */
} pc_ui_manual_topic;

/* The illustrations' PlayStation button art (HELP01's controller legend and its repeats), recognised by
 * a hash of the composited pixels (a dev-time table: pc/tools/manual_dump.py --art): the reader draws the
 * player's device prompts in their place. */
enum {
    PC_UI_MANUAL_ART_TRIANGLE, PC_UI_MANUAL_ART_CIRCLE, PC_UI_MANUAL_ART_CROSS, PC_UI_MANUAL_ART_SQUARE,
    PC_UI_MANUAL_ART_L1, PC_UI_MANUAL_ART_L2, PC_UI_MANUAL_ART_R1, PC_UI_MANUAL_ART_R2,
    PC_UI_MANUAL_ART_START, PC_UI_MANUAL_ART_SELECT, PC_UI_MANUAL_ART_DPAD, PC_UI_MANUAL_ARTS
};
/* FNV-1a 64 of w, h (16 bits each, little endian) and the w * h * 4 RGBA bytes. */
unsigned long long pc_ui_manual_art_hash(const unsigned char* rgba, int w, int h);
/* PC_UI_MANUAL_ART_* for an image, -1 when it is not button art. */
int pc_ui_manual_button_art(const unsigned char* rgba, int w, int h);

/* Parse a topic. widths: the game's character advances (vs_battle_characterWidths), nwidths entries
 * (189 per font table). hf1 may be null (no illustrations). Every offset is checked; a bad entry is
 * skipped and counted (unknown). Returns out->ok (0: the header or text table is broken; out is then
 * empty). */
int pc_ui_manual_parse(const unsigned char* hf0, unsigned n0, const unsigned char* hf1, unsigned n1,
    const unsigned char* widths, unsigned nwidths, pc_ui_manual_topic* out);
void pc_ui_manual_free(pc_ui_manual_topic* t);

/* 494.c _getRoundedLineCount: max(0, n rounded up to 10, minus 10). */
int pc_ui_manual_line_count(int n);
/* 494.c _setMenuRows over 14 rows (9 shown): the list's row and scroll for `row`. */
void pc_ui_manual_seed(int row, int* cm4, int* cm5);
/* MAINMENU 58EC.c's list (14 rows, rowInfo 1): Up then Down of one frame; wraps on a press only.
 * Returns 1 when row + page changed (the change sound). */
int pc_ui_manual_list_step(int* row, int* page, int up_rep, int up_press, int down_rep, int down_press);
/* 494.c showSubMenu's scroll step (Start, Square held: pages, else lines). Returns 1 when the change sound
 * plays. */
int pc_ui_manual_step(int* scroll, int line_count, int square_held, int start, int up_rep, int up_press,
    int down_rep, int down_press);
/* 494.c _getAnimationState: one step; returns 1 while the element is hidden (isActive). An entry with
 * enabled 0 is always shown (the caller does not step it). */
int pc_ui_manual_anim_step(pc_ui_manual_anim* a);

#ifdef __cplusplus
}
#endif
#endif
