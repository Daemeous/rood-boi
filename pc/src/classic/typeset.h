/* Typography decided by a classic draw (pc/src/classic; plan
 * pc/docs/plans/2026-10-20-classic-ui-in-lua.md §2.5): how ui_text sets the
 * glyph packets of one range, handed over with them. The packets stay the
 * game's (the parity gate compares them); ui_text restyles them at raster
 * time with these instead of its own defaults (its settings). Plain C: shared by
 * hud.h (the classic commands) and ui_text.c. */
#ifndef ROOD_CLASSIC_TYPESET_H
#define ROOD_CLASSIC_TYPESET_H

/* The battle's status words (BATTLE.PRG's raw printer, set as one word in the
 * status style: ui_text.c pc_text_status). A field whose bit is not in `set`
 * keeps ui_text's default (the setting named). */
enum {
    PC_TYPESET_BLANKS = 1,     /* blanks: 1 a run of the game's narrow blanks is one space (WELL   TIMED!), 0 each is one */
    PC_TYPESET_KEEP = 2,       /* keep_on_screen: 1 moved only as far as it takes to stay on the screen */
    PC_TYPESET_SCREEN_W = 4,   /* screen_w: the screen kept to (the field's 320 pixels) */
    PC_TYPESET_MARGIN = 8,     /* margin: pixels beyond the outline kept from its edges (ui_text_edge_margin) */
    PC_TYPESET_WORD_SPACE = 16 /* word_space: the font's space times this (ui_text_word_space) */
};
typedef struct {
    unsigned set;
    int blanks, keep_on_screen;
    float screen_w, margin, word_space;
} pc_text_typeset;

#endif
