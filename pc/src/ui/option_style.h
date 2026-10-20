/* The Option screens' look and feel as data (pc/src/ui/option_style.c; plan
 * pc/docs/plans/2026-10-18-lua-m1-options.md): the classic pages' layout in
 * the game's Option screen (classic_options.c) and the "Keep these
 * settings?" countdown (display_confirm.h). The shipped rood.options plugin
 * sets every value from its Lua table (rood.menu.style); the defaults here
 * are the same values, for when it is off. RPC options {style} reads and
 * sets them live. Positions are the PS1's 320 x 240 screen pixels. */
#ifndef PC_OPTION_STYLE_H
#define PC_OPTION_STYLE_H
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_STYLE_ROW_X,        /* classic_row_x: a page row's window left edge (it reaches the screen's right edge) */
    PC_STYLE_ROW_W,        /* classic_row_w: its width (the game's windows are at most 255) */
    PC_STYLE_VALUE_X,      /* classic_value_x: the value column, from the window's left edge */
    PC_STYLE_UPPERCASE,    /* classic_uppercase: labels and values in capitals, as the game's own rows */
    PC_STYLE_TOGGLE_GAP,   /* classic_toggle_gap: between ON and OFF */
    PC_STYLE_BAR_COLOUR,   /* classic_bar_colour: the slider's gauge (the HP/MP/RISK bars' colours 0-3) */
    PC_STYLE_BAR_WIDTH,    /* classic_bar_width: the gauge's length at its maximum (the game's bars: 64) */
    PC_STYLE_LIT,          /* classic_lit: the current value's text colour (0 white, 1 the game's lit yellow, 3 grey) */
    PC_STYLE_OTHER,        /* classic_other: the other toggle value's */
    PC_STYLE_HELP_WIDTH,   /* classic_help_width: a help line's width in the information box */
    PC_STYLE_HELP_SECONDS, /* classic_help_seconds: a longer help's lines take turns in the box (it holds one) */
    PC_STYLE_CONFIRM_SECONDS, /* confirm_seconds: "Keep these settings?" reverts after this long */
    PC_STYLE_COUNT
};

int pc_option_style_count(void);
const char* pc_option_style_name(int i);                /* "classic_row_x"; NULL past the last */
const char* pc_option_style_help(int i);
int pc_option_style_index(const char* name);            /* -1: none */
double pc_option_style_value(int i);                    /* the default for an unknown index */
double pc_option_style_default(int i);
int pc_option_style_int(int i);                         /* rounded */
/* Set a value (clamped to its range); 1 when the name is known. */
int pc_option_style_set(const char* name, double value);
/* Every value back to its default. */
void pc_option_style_reset(void);
/* Bumped by every change (renderers re-read). */
unsigned pc_option_style_generation(void);
/* {"classic_row_x":65,...}; returns the length. */
int pc_option_style_json(char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
