/* The full lines of the game's text boxes (ui_text.c, ui_text_bubble_align
 * centre): a talk bubble types its page out a character at a time, so the
 * glyphs drawn in a frame are only the start of each line. To centre a line
 * where it will stay, ui_text.c asks for the whole line the game will draw:
 * the current page of each text box is walked as _printVariableWidthFont
 * (BATTLE 5BF94.c) walks it, every character counted (as if fully typed
 * out), and the line starting at the given glyph is returned with each
 * glyph's index (font table included) and place.
 *
 * Compiled with the game headers (battle builds); ui_text.c links it weakly. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"

#define BOXES 8 /* vs_battle_textBoxes: 0x180 bytes */
#define LINE_MAX 128

int pc_text_box_line(int x, int y, int glyph, int* glyphs, int* xs, int max);

/* One page of `box` walked; for the line at y whose first glyph is `glyph`
 * at x: its glyphs into glyphs/xs, their count returned (0: no such line). */
static int walk(vs_battle_textBox* box, int x, int y, int glyph, int* glyphs, int* xs, int max)
{
    const u_char* str = (const u_char*)psx_get(box->string);
    const u_char* saved = NULL;
    char number[16];
    int table = box->unk0.unk0_0 != 4, justify = 0, printX = box->xIndent, printY = box->yIndent, count = 0;
    int mismatch = 0, steps = 0;
    if (box->unk0.unk0_0 == 2)
        table = 0;
    if (!str)
        return 0;
    for (;;) {
        u_int c;
        if (++steps > 4096)
            return 0;
        c = *str++;
        if (c < 0xEC) {
            if (c < 0xE5) { /* a glyph (0x8F: a space) */
                if (c == 0x8F) {
                    printX += 6;
                    continue;
                }
                {
                    int id = (int)c + table * 189;
                    if (printY == y && !mismatch && count < max) {
                        if (!count && (id != glyph || printX != x))
                            mismatch = 1; /* another line at this height (another box) */
                        else
                            glyphs[count] = id, xs[count] = printX, ++count;
                    }
                    printX += vs_battle_characterWidths[id];
                }
                continue;
            }
            if (c == 0xE8) { /* new line */
                if (count)
                    return count;
                printX = box->xIndent;
                if (justify)
                    printX += ((box->charsPerLine * 2) - vs_battle_getTextLineLength((char*)str)) * 3;
                printY += 13;
                mismatch = 0;
                continue;
            }
            if (c == 0xE7 && saved) { /* the end of an inline string */
                str = saved, saved = NULL;
                continue;
            }
            if (c == 0xE5 || c == 0xE7)
                return count; /* the page ends */
            continue; /* 0xE6 (the arrow) and 0xE9-0xEB draw no glyph here */
        }
        {
            u_int param = *str++;
            switch (c) {
            case 0xFA: /* offset x */
                printX += param >= 240 ? (int)param - 256 : (int)param;
                break;
            case 0xFB:
                if (param & 0xF8)
                    printY += param >> 3;
                else if (param == 6)
                    table = 0;
                else if (param == 5)
                    table = 1;
                else if (param == 4) {
                    justify ^= 1;
                    if (justify)
                        printX = ((box->charsPerLine * 2) - vs_battle_getTextLineLength((char*)str)) * 3
                               + box->xIndent;
                }
                break;
            case 0xFD:
            case 0xFE: { /* numbers: hex, decimal */
                int value = vs_battle_stringContext.integers[param % 10], n = 0, base = c == 0xFE ? 10 : 16;
                int digits[12], k = 0;
                if (value < 0)
                    value = -value, number[n++] = (char)0xA7;
                if (param < 10) {
                    do
                        digits[k++] = value % base, value /= base;
                    while (value && k < 12);
                } else {
                    int want = (int)(param / 10);
                    for (k = 0; k < want && k < 12; k++)
                        digits[k] = value % base, value /= base;
                }
                while (k && n < 15)
                    number[n++] = (char)digits[--k];
                number[n] = (char)0xE7;
                if (!saved) {
                    saved = str;
                    str = (const u_char*)number;
                }
                break;
            }
            case 0xFF:
                if (!saved) {
                    saved = str;
                    str = (const u_char*)psx_get(vs_battle_stringContext.strings[param]);
                    if (!str)
                        return 0;
                }
                break;
            default: /* 0xF8 speed, 0xF9 sound, 0xFC: nothing drawn */
                break;
            }
        }
    }
}

int pc_text_box_line(int x, int y, int glyph, int* glyphs, int* xs, int max)
{
    if (max > LINE_MAX)
        max = LINE_MAX;
    for (int i = 0; i < BOXES; i++) {
        vs_battle_textBox* box = &vs_battle_textBoxes[i];
        if (!psx_get(box->string))
            continue;
        int n = walk(box, x, y, glyph, glyphs, xs, max);
        if (n)
            return n;
    }
    return 0;
}
