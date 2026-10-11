/* Portable packet emitter for 5BF94.c's hand-written fixed-width glyph routine.
 * Positions and advances remain 16.16; the atlas uses 21 columns of 12 pixels. */
extern unsigned char _fontTable, D_800F4CB8, D_800F4E80;
extern unsigned _fontBrightness;
extern unsigned char vs_battle_characterWidths[];
/* A 32-bit guest word holding the OT base (BATTLE 0x800F51B8). */
extern unsigned D_800F51B8;
extern void pc_ui_precise_quad(const void* packet, const float* corners) __attribute__((weak));
int pc_fixed_font_char(unsigned** cursor, int character, unsigned x, unsigned y,
    unsigned width, unsigned height, unsigned scale)
{
    if (character == 0x8f)
        return x + scale * 6;
    unsigned glyph = character + _fontTable * 189;
    /* The cursor is the scratchpad's 4-byte packet pointer. */
    unsigned* packet = psx_load(unsigned*, cursor);
    unsigned* head = psx_to_ptr(unsigned*, D_800F51B8) + D_800F4E80 * 4 + 3;
    unsigned clut = (((D_800F4CB8 * 16 + 0x340) >> 4) & 63) | 0x3780;
    unsigned row = glyph / 21, column = glyph % 21;
    unsigned u0 = column * 12, u1 = u0 + 12;
    unsigned v0 = row * 12 << 8, v1 = v0 + 0xc00;
    packet[0] = 0x09000000 | (*head & 0xffffff);
    packet[1] = _fontBrightness;
    packet[2] = (x >> 16) | (y & 0xffff0000);
    packet[3] = u0 | v0 | (clut << 16);
    packet[4] = ((x + width) >> 16) | (y & 0xffff0000);
    packet[5] = u1 | v0 | 0x000d0000;
    packet[6] = (x >> 16) | ((y + height) & 0xffff0000);
    packet[7] = u0 | v1;
    packet[8] = ((x + width) >> 16) | ((y + height) & 0xffff0000);
    packet[9] = u1 | v1;
    if (pc_ui_precise_quad) {
        /* The unrounded corners (16.16), for sub-pixel text (ui_upscale.c). */
        float x0 = (int)x / 65536.0f, y0 = (int)y / 65536.0f, x1 = (int)(x + width) / 65536.0f,
              y1 = (int)(y + height) / 65536.0f;
        float corners[8] = { x0, y0, x1, y0, x0, y1, x1, y1 };
        pc_ui_precise_quad(packet + 1, corners); /* the command, as the GPU walker sees it */
    }
    *head = psx_to_int(unsigned, packet) & 0xffffff;
    psx_store(cursor, packet + 10);
    return x + vs_battle_characterWidths[glyph] * scale;
}
int _printFixedWidthFontChar(
    int character, int x, int y, int width, int height, int scale)
{
    return pc_fixed_font_char((void*)0x1f800000, character, x, y, width, height, scale);
}
