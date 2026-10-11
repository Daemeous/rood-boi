#include <stdio.h>
unsigned char _fontTable, D_800F4CB8, D_800F4E80;
unsigned _fontBrightness = 0x2c808080;
unsigned char vs_battle_characterWidths[512];
static unsigned ot[16], packets[20];
unsigned D_800F51B8;
int pc_fixed_font_char(unsigned**, int, unsigned, unsigned, unsigned, unsigned, unsigned);
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    unsigned* cursor = packets;
    D_800F51B8 = (unsigned)ot;
    D_800F4E80 = 2;
    D_800F4CB8 = 4;
    ot[11] = 0x12abcdef;
    vs_battle_characterWidths[22] = 7;
    CHECK(pc_fixed_font_char(&cursor, 22, 10 << 16, 20 << 16, 12 << 16, 12 << 16, 1 << 16)
          == 17 << 16);
    CHECK(cursor == packets + 10 && packets[0] == 0x09abcdef && packets[1] == 0x2c808080);
    CHECK(packets[2] == 0x0014000a && packets[4] == 0x00140016 && packets[6] == 0x0020000a
          && packets[8] == 0x00200016);
    CHECK(packets[3] == 0x37b80c0c && packets[5] == 0x000d0c18 && packets[7] == 0x180c
          && packets[9] == 0x1818);
    CHECK(ot[11] == ((unsigned)packets & 0xffffff));
    CHECK(pc_fixed_font_char(&cursor, 0x8f, 17 << 16, 0, 0, 0, 1 << 15) == 20 << 16
          && cursor == packets + 10);
    _fontTable = 1;
    vs_battle_characterWidths[189] = 9;
    CHECK(pc_fixed_font_char(&cursor, 0, 0xffff8000, 1 << 15, 6 << 16, 6 << 16, 1 << 15)
          == 4 << 16);
    CHECK(packets[12] == 0xffff && packets[14] == 5 && packets[16] == 0x0006ffff);
    CHECK(packets[13] == 0x37b86c00 && packets[15] == 0x000d6c0c);
    puts(
        "PASS: fixed font atlas selection, UVs, CLUT, OT links, fractional/negative placement and space advance");
    return 0;
}
