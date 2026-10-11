#include <sys/types.h>
#include <libgte.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void pc_actor_shadow(unsigned char*, unsigned char*);
signed char D_800F49F4;
unsigned D_800E9A68[18], D_800E9AD4[24];
static int calls, blocked_call, slope_x;
int func_800A6EE8(SVECTOR* position, int dx, int dz, int mode)
{
    (void)position;
    (void)dz;
    if (mode != 3)
        exit(2);
    D_800F49F4 = ++calls == blocked_call;
    return dx / 4 * slope_x;
}
void pc_unimplemented(const char* name)
{
    printf("unexpected trap: %s\n", name);
    exit(2);
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned actor_words[0x1900 / 4], scratch_words[0x1000 / 4], packets[24 * 8],
    ot[2048];
static unsigned char* actor = (void*)actor_words;
static unsigned char* scratch = (void*)scratch_words;
static void setup(void)
{
    memset(actor_words, 0, sizeof(actor_words));
    memset(scratch_words, 0, sizeof(scratch_words));
    memset(packets, 0, sizeof(packets));
    for (unsigned i = 0; i < 2048; ++i)
        ot[i] = 0x12ffffff;
    *(unsigned**)scratch = packets;
    *(unsigned**)(scratch + 4) = ot;
    *(MATRIX*)(scratch + 0x14) =
        (MATRIX) { { { 4096, 0, 0 }, { 0, 0, -4096 }, { 0, 4096, 0 } }, { 0, 0, 1000 } };
    *(unsigned short*)(actor + 0x64c) = 32;
    *(unsigned*)(actor + 0x2c) = 0x10001000;
    *(unsigned*)(actor + 0x30) = 4096;
    *(short*)(actor + 0x1870) = *(short*)(actor + 0x1872) = 8192;
    *(unsigned*)(actor + 0x1864) = 0x808080;
    D_800E9A68[0] = 0;
    D_800E9A68[1] = 100;
    D_800E9A68[2] = 100u << 16;
    for (unsigned i = 0; i < 24; ++i)
        D_800E9AD4[i] = 0x080400;
    calls = blocked_call = slope_x = 0;
    InitGeom();
    SetGeomOffset(0, 0);
    SetGeomScreen(1000);
}
int main(void)
{
    setup();
    pc_actor_shadow(actor, scratch);
    CHECK(calls == 5 && *(unsigned**)scratch == packets + 192);
    CHECK(*(unsigned*)(actor + 0x6fc) == 0 && *(unsigned*)(actor + 0x700) == 1000);
    CHECK(packets[0] == 0x07ffffff && packets[1] == 0xe1000640);
    CHECK(packets[2] == 0x32c0c0c0 && packets[4] == 0x606060 && packets[6] == 0x606060);
    CHECK(packets[3] == 0 && packets[5] == 100 && (short)(packets[7] >> 16) == -100);
    CHECK(packets[8 * 8 + 2] == 0x32606060 && packets[8 * 8 + 4] == 0);
    CHECK(packets[16 * 8 + 2] == 0x32000000 && packets[16 * 8 + 4] == 0x32606060);
    CHECK(ot[248] == (0x12000000 | ((unsigned)(packets + 23 * 8) & 0xffffff)));
    setup();
    slope_x = 1;
    pc_actor_shadow(actor, scratch);
    CHECK(calls == 5 && *(unsigned**)scratch == packets + 192 && packets[5] != 100);
    setup();
    slope_x = 1;
    blocked_call = 2;
    pc_actor_shadow(actor, scratch);
    CHECK(calls == 2 && packets[5] == 100);
    setup();
    slope_x = 100;
    pc_actor_shadow(actor, scratch);
    CHECK(packets[5] == 100); /* Steep terrain clamps back to a flat shadow. */
    setup();
    actor[0x6f2] = 100;
    pc_actor_shadow(actor, scratch);
    CHECK(packets[2] == 0x32000000);
    setup();
    actor[0x6f2] = (unsigned char)-100;
    pc_actor_shadow(actor, scratch);
    CHECK(packets[2] == 0x32ffffff);
    setup();
    for (unsigned i = 0; i < 24; ++i)
        D_800E9AD4[i] = 0x040800;
    pc_actor_shadow(actor, scratch);
    CHECK(*(unsigned**)scratch == packets);
    setup();
    ((MATRIX*)(scratch + 0x14))->t[2] = 9000;
    pc_actor_shadow(actor, scratch);
    CHECK(*(unsigned**)scratch == packets);
    puts(
        "PASS: shadow projection, terrain slopes/fallback, ring colors, fade, culling and OT links");
    return 0;
}
