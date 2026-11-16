/* Portable packet emitter for 5BF94.c's hand-written fixed-width glyph routine.
 * Positions and advances remain 16.16; the atlas uses 21 columns of 12 pixels. */
extern unsigned char _fontTable, D_800F4CB8, D_800F4E80;
extern unsigned _fontBrightness;
extern unsigned char vs_battle_characterWidths[];
/* A 32-bit guest word holding the OT base (BATTLE 0x800F51B8). */
extern unsigned vs_menu_activeTextBoxOrderingTable;
extern void pc_ui_precise_quad(const void* packet, const float* corners) __attribute__((weak));
/* ui_text_layout line (ui_text.c): which glyphs one string sets. */
extern void pc_text_glyph(void* const* commands, int count, int glyph, int x, int y, int next_x, int advance)
    __attribute__((weak));
int pc_fixed_font_char(void** cursor, int character, unsigned x, unsigned y, unsigned width, unsigned height, unsigned scale);

int _printFixedWidthFontChar( int character, int x, int y, int width, int height, int scale);

