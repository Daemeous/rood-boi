#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void CTC2(unsigned, unsigned);
unsigned* pc_weapon_packets(unsigned*, unsigned*, const unsigned*, const unsigned*,
    const unsigned*, const unsigned short[3], unsigned, unsigned, unsigned);
void pc_unimplemented(const char* name)
{
    printf("unexpected trap: %s\n", name);
    exit(2);
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static unsigned packets[128], table[2056];
int main(void)
{
    unsigned* ot = table + 8;
    for (int i = 0; i < 2056; i++)
        table[i] = 0x12ffffff;
    CTC2(341, 29);
    unsigned xy[4] = { 0, 10, 10u << 16, 10 | (10u << 16) };
    unsigned z[4] = { 1000, 1000, 1000, 1000 };
    unsigned faces[] = {
        0x1024,
        4u << 16,
        (0x0605u << 16) | 8,
        0x04030201,
        0x142c,
        4u << 16,
        (12u << 16) | 8,
        0x04030201,
        0x08070605,
        0x0059142c,
        4u << 16,
        (12u << 16) | 8,
        0x04030201,
        0x08070605,
    };
    unsigned short counts[3] = { 1, 1, 1 };
    CHECK(pc_weapon_packets(packets, ot, xy, z, faces, counts, 0x123456, 7, 9)
          == packets + 28);
    CHECK(packets[0] == 0x07ffffff && packets[1] == 0x24123456);
    CHECK(packets[3] == 0x00090201 && packets[5] == 0x00070403 && packets[7] == 0x0605);
    CHECK(packets[9] == 0x2c123456 && packets[15] == 0x08070605 && packets[17] == 0x0807);
    CHECK(packets[19] == 0x2e123456 && packets[23] == 0x00470403);
    CHECK(ot[248] == (0x12000000 | ((unsigned)packets & 0xffffff)));
    CHECK(ot[249] == (0x12000000 | ((unsigned)(packets + 8) & 0xffffff)));
    CHECK(ot[253] == (0x12000000 | ((unsigned)(packets + 18) & 0xffffff)));
    unsigned reverse[4] = { 0, 10u << 16, 10, 10 | (10u << 16) };
    CHECK(pc_weapon_packets(packets, ot, reverse, z, faces, counts, 0, 7, 9)
          == packets + 10);
    for (int i = 0; i < 4; i++)
        z[i] = 65535;
    CHECK(pc_weapon_packets(packets, ot, xy, z, faces, counts, 0, 7, 9) == packets);
    for (int i = 0; i < 4; i++)
        z[i] = 1000;
    unsigned split[] = { 0x0003142c, 4u << 16, (12u << 16) | 8, 0x04030201, 0x08070605 };
    unsigned short split_count[3] = { 0, 0, 1 };
    CHECK(pc_weapon_packets(packets, ot, xy, z, split, split_count, 0xabcdef, 7, 9)
          == packets + 16);
    CHECK(packets[2] == xy[0] && packets[4] == xy[1] && packets[6] == xy[3]);
    CHECK(packets[10] == xy[0] && packets[12] == xy[3] && packets[14] == xy[2]);
    z[3] = 1100;
    CHECK(pc_weapon_packets(packets, ot, xy, z, split, split_count, 0xabcdef, 7, 9)
          == packets + 16);
    CHECK(packets[2] == xy[0] && packets[4] == xy[1] && packets[6] == xy[2]);
    CHECK(packets[10] == xy[1] && packets[12] == xy[3] && packets[14] == xy[2]);
    puts(
        "PASS: weapon packet layout, UVs, blend flags, OT depth/links, culling, far clip and both split diagonals");
    return 0;
}
