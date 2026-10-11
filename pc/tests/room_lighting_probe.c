#include <stdio.h>
#include <stdlib.h>
int pc_room_light(unsigned, int, unsigned*, const unsigned*, int);
int func_80098C3C(unsigned, unsigned char*, const unsigned*);
void pc_unimplemented(const char* s)
{
    printf("unexpected trap: %s\n", s);
    exit(2);
}
#define XY(x, z) ((unsigned short)(x)|((unsigned)(unsigned short)(z)<<16))
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    unsigned table[] = { 0x123456, 1, 1, XY(0, 0), XY(10, 10), XY(0, 0), XY(0, 10),
        XY(10, 0), 200, 100 << 8, 100 << 16, XY(-10, -10), XY(0, 0), XY(-10, -10),
        XY(-10, 0), XY(0, -10), 0x808080, 0x808080, 0x808080 };
    unsigned color = 0xab000000;
    CHECK(pc_room_light(XY(2, 3), 0, &color, table, 1) == 1 && color == 0xab141e64);
    CHECK(pc_room_light(XY(0, 0), 0, &color, table, 1) == 1 && color == 0xab0000c8);
    CHECK(pc_room_light(XY(10, 10), 0, &color, table, 1) == 0 && color == 0x123456);
    CHECK(pc_room_light(XY(-2, 3), 0, &color, table, 1) == 0 && color == 0x123456);
    CHECK(pc_room_light(XY(-8, -7), 1, &color, table, 1) == 1 && color == 0x808080);
    CHECK(pc_room_light(0, 0, &color, table, 0) == 0 && color == 0x808080);
    CHECK(pc_room_light(0, 0, &color, 0, 1) == 0 && color == 0x808080);
    table[7] = table[6];
    color = 0xaabbccdd;
    CHECK(func_80098C3C(0, (void*)&color, table + 3) == 0 && color == 0xaabbccdd);
    puts(
        "PASS: room light interpolation, vertex/edge coverage, signed bounds, layer choice, fallback and degenerate rejection");
    return 0;
}
