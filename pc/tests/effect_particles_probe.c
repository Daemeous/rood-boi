#include "common.h"
#include "src/BATTLE/BATTLE.PRG/5BF94.h"
#include "src/SLUS_010.40/main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inline_c.h>

/* Scratch is an ordinary global here: no guest address or game assets required. */
vs_scratch_t vs_scratch;
D_800F569C_t context;
D_800F569C_t* D_800F569C = &context;
D_800F53B8_t* D_800F53BC;
int vs_main_projectionDistance = 256, vs_main_nearClip = 8;
u_char D_80040A14[513];
void func_800D6E44(void*);
void pc_unimplemented(const char* name)
{
    printf("unexpected trap: %s\n", name);
    exit(2);
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned packets[128], ot[2048];
int main(void)
{
    func_800D2904_t node = { 0 };
    func_800D6CF_t* sprite = (void*)node.unk3C;
    unsigned command = 0x30000000;
    pFileBlock1Indices indices = { 1, 0 };
    pFileBlock1Data tile = { 0 };
    unsigned curves[10] = { 0 };
    sprite->unk18 = (void*)&command;
    sprite->unk1C = &indices;
    node.unk14 = 1024 << 12;
    node.unk60_0 = 16;
    tile.clut = 5;
    tile.tpage = 7;
    tile.u0 = 16;
    tile.v0 = 32;
    tile.u1 = 8;
    tile.v1 = 8;
    short* corners = (void*)tile.unk8;
    corners[0] = -8;
    corners[1] = -8;
    corners[2] = 8;
    corners[3] = -8;
    corners[4] = -8;
    corners[5] = 8;
    corners[6] = 8;
    corners[7] = 8;
    context.block1Data = &tile;
    context.curveBlock = (void*)curves;
    vs_scratch.viewMatrix.m[0][0] = 4096;
    vs_scratch.viewMatrix.m[1][1] = 4096;
    vs_scratch.viewMatrix.m[2][2] = 4096;
    SetGeomScreen(256);
    SetGeomOffset(160, 112);
    for (int i = 0; i < 2048; i++)
        ot[i] = 0xffffff;
    vs_scratch.unk0 = (void*)packets;
    vs_scratch.unk4 = (void*)ot;
    func_800D6E44(&node);
    CHECK(vs_scratch.unk0 == (void*)(packets + 16));
    CHECK(packets[0] == 0x01ffffff && packets[1] == 0xe1000200);
    CHECK(packets[4] == 0x2c808080);
    CHECK(packets[5] == (152u | (104u << 16)));
    CHECK(packets[7] == (168u | (104u << 16)));
    CHECK(packets[9] == (152u | (120u << 16)));
    CHECK(packets[11] == (168u | (120u << 16)));
    CHECK(packets[6] == (16u | (32u << 8) | (5u << 16)));
    CHECK(packets[8] == (24u | (32u << 8) | (7u << 16)));
    CHECK(packets[14] == 0xe1000000);
    CHECK((ot[256] & 0xffffff) == ((unsigned)(packets + 13) & 0xffffff));
    vs_scratch.unk0 = (void*)packets;
    node.unk14 = 4 << 12;
    func_800D6E44(&node);
    CHECK(vs_scratch.unk0 == (void*)packets);
    node.unk14 = 1024 << 12;
    tile.clut |= 0x8000;
    func_800D6E44(&node);
    CHECK(packets[4] == 0x2e808080);
    u_char red[2] = { 1, 2 }, green[2] = { 3, 4 }, blue[2] = { 0, 0 };
    context.curveBlock->curveSizes[0] = 2;
    context.curveBlock->curveSizes[1] = 2;
    context.curveBlock->curveSizes[2] = 2;
    context.curves[0] = red;
    context.curves[1] = green;
    context.curves[2] = blue;
    node.unk74.fields.rCurve = 1;
    node.unk74.fields.gCurve = 2;
    node.unk74.fields.bCurve = 3;
    node.unk74.fields.age = 0;
    vs_scratch.unk0 = (void*)packets;
    func_800D6E44(&node);
    CHECK(packets[4] == 0x2e000703);
    command = 0x00020000;
    context.block1Tables[0] = &indices;
    sprite->unkE = 0;
    func_800D6E44(&node);
    CHECK(sprite->unk6 == 2 && sprite->unkE == 4 && sprite->unk1C == &indices);
    puts(
        "PASS: particle projection, packet layout, texture coordinates, OT link, clipping and blend flag");
    return 0;
}
