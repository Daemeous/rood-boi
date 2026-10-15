/* Modern menus: the menu tint (System > Interface > Menu tint; settings
 * ui_menu_tint, ui_menu_tint_custom; pc/docs/Modern_Menus.md "Tint"). The
 * colour of the menus' backdrop and panels: a preset or a custom colour,
 * 0..31 per channel as the PlayStation's own 5-bit colours. Pure (no theme or
 * ImGui): ui_theme.cpp theme() derives the shades from it. */
#ifndef PC_UI_TINT_H
#define PC_UI_TINT_H
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_UI_TINT_LEA_MONDE, /* the theme file's own colours, untouched (the default) */
    PC_UI_TINT_CLASSIC,   /* deep blue, as classic RPG windows */
    PC_UI_TINT_SLATE,
    PC_UI_TINT_CRIMSON,
    PC_UI_TINT_VERDANT,
    PC_UI_TINT_CUSTOM,    /* pc_ui_menu_tint_custom */
    PC_UI_TINTS
};
#define PC_UI_TINT_MAX 31

extern int pc_ui_menu_tint;           /* PC_UI_TINT_* (project_settings.cpp) */
extern int pc_ui_menu_tint_custom[3]; /* Custom's colour, 0..31 per channel */

const char* pc_ui_tint_id(int tint);   /* "lea_monde" ... "custom"; NULL out of range */
const char* pc_ui_tint_name(int tint); /* "Leá Monde", "Classic", ...; NULL out of range */
const char* const* pc_ui_tint_names(void);
int pc_ui_tint_from_name(const char* s); /* an id or a digit 0..5; -1 unknown */
int pc_ui_tint_current(void);            /* pc_ui_menu_tint (out of range: Leá Monde) */
/* A tint's colour, 0..31 per channel (Custom: pc_ui_menu_tint_custom, clamped;
 * Leá Monde: the theme file's live ground (ui_theme.cpp
 * pc_ui_theme_file_ground_rgb) as v * 31 / 64 rounded, for the chip and sliders). */
void pc_ui_tint_rgb(int tint, int out[3]);
void pc_ui_tint_effective(int out[3]); /* the current tint's */
/* The colour as a chip (0..255, v * 255 / 31 rounded): the vivid tint the row shows. */
void pc_ui_tint_swatch(const int rgb31[3], unsigned char out[3]);
/* The menus' ground for the colour, 0..255 scale (v * 64 / 31, unrounded): dark. */
void pc_ui_tint_ground(const int rgb31[3], float out[3]);

#ifdef __cplusplus
}
#endif
#endif
