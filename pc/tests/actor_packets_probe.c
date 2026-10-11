#include <stdio.h>
#include <stdlib.h>
void CTC2(unsigned, unsigned);
unsigned* pc_actor_packets(unsigned*, unsigned*, const unsigned*, const unsigned*,
    const unsigned*, const unsigned short[3], unsigned, unsigned);
void pc_unimplemented(const char* s)
{
    printf("unexpected trap: %s\n", s);
    exit(2);
}
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned packets[128], table[2056];
int main(void)
{
    unsigned* ot = table + 8;
    for (int i = 0; i < 2056; ++i)
        table[i] = 0x12ffffff;
    unsigned xy[] = { 0, 10, 10u << 16, 10 | (10u << 16) },
             z[] = { 1000, 1000, 1000, 1000 };
    unsigned faces[] = { 4u << 16, 8 | (0x0605u << 16), 0x34123456, 0x18654321,
        0x00112233, 0x04030201, 4u << 16, 8 | (12u << 16), 0x3c123456, 0x20654321,
        0x00112233, 0x00445566, 0x04030201, 0x08070605, 4u << 16, 8 | (0x0605u << 16),
        0x34123456, 0x18654321, 0x59112233, 0x04030201 };
    unsigned short counts[] = { 1, 1, 1 };
    CTC2(341, 29);
    CHECK(pc_actor_packets(packets, ot, xy, z, faces, counts, 7, 9) == packets + 33);
    CHECK(
        packets[0] == 0x09ffffff && packets[1] == 0x34123456 && packets[4] == 0x18654321);
    CHECK(packets[3] == 0x00090201 && packets[6] == 0x00070403 && packets[9] == 0x0605);
    CHECK(packets[10] == 0x0cffffff && packets[11] == 0x3c123456
          && packets[19] == 0x08070605 && packets[22] == 0x0807);
    CHECK(packets[24] == 0x36123456 && packets[29] == 0x00470403);
    CHECK(ot[248] == (0x12000000 | ((unsigned)packets & 0xffffff)));
    CHECK(ot[249] == (0x12000000 | ((unsigned)(packets + 10) & 0xffffff)));
    CHECK(ot[252] == (0x12000000 | ((unsigned)(packets + 23) & 0xffffff)));
    unsigned special_quad[8];
    for (unsigned i = 0; i < 8; ++i)
        special_quad[i] = faces[6 + i];
    special_quad[4] = 0x51112233;
    unsigned short special_count[] = { 0, 0, 1 };
    CHECK(pc_actor_packets(packets, ot, xy, z, special_quad, special_count, 7, 9)
          == packets + 13);
    CHECK(
        packets[1] == 0x3e123456 && packets[6] == 0x00470403 && packets[9] == 0x08070605);
    unsigned reverse[] = { 0, 10u << 16, 10, 10 | (10u << 16) };
    CHECK(pc_actor_packets(packets, ot, reverse, z, faces, counts, 7, 9) == packets + 10);
    for (int i = 0; i < 4; ++i)
        z[i] = 65535;
    CHECK(pc_actor_packets(packets, ot, xy, z, faces, counts, 7, 9) == packets);
    puts(
        "PASS: actor GT3/GT4 colors, UVs, OT links, special blend/depth flags, culling and far clip");
    return 0;
}
