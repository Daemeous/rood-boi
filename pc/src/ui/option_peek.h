/* The Option pages' preview, a "peek" (pc/src/ui/option_peek.c): while a row
 * that offers one (pc_ui_setting_row.preview: every Graphics setting,
 * Brightness, a page row's `preview = true`) is focused and Square is held,
 * the menu steps aside: its layers (the game's 2D layer: windows, text, the
 * information box; the HUD; the modern menu and its backdrop) are hidden,
 * so the live 3D view shows the value. (The classic menu draws no dimming
 * layer of its own: the field looks darker under it because the game holds
 * its effects while a menu is open, torch flames included, and their glow
 * lights go with them; that stays so in the preview.) Left / Right still
 * change it; a small readout names the row and its value. Releasing Square
 * (or leaving the row, the page, the menu) brings the menu back exactly. The
 * field does not advance: the menu keeps the game paused, and the 3D view it
 * shows is redrawn every frame (occlusion, post and lighting included). Both
 * Option screens drive it (classic_options.c, ui_shell.cpp). */
#ifndef PC_OPTION_PEEK_H
#define PC_OPTION_PEEK_H
#ifdef __cplusplus
extern "C" {
#endif

/* Every frame a page is up: whether the peek is on now, and the focused
 * row's label and value (copied). Off hides nothing. */
void pc_option_peek(int on, const char* label, const char* value);
/* On now (and fresh: an Option screen said so within the last frames). */
int pc_option_peek_active(void);
/* The readout's text (label, value) while on; 0 when off. */
int pc_option_peek_text(const char** label, const char** value);
/* The help line's hint for a row that offers a preview. */
extern const char* const pc_option_peek_hint;

#ifdef __cplusplus
}
#endif
#endif
