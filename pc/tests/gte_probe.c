#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include <libetc.h>
#include <inline_c.h>
#include <stdio.h>
#include <stdlib.h>
void pc_unimplemented(const char* name)
{
    printf("unexpected trap: %s\n", name);
    exit(2);
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)
/* Synthetic reciprocal-sqrt fixtures: unity and 1/sqrt(2), Q12. */
short D_80040D98[192] = { [0] = 4096, [64] = 2896 };
MATRIX* func_80041C68(MATRIX*, MATRIX*);
MATRIX* func_800B17F0(SVECTOR*, MATRIX*);
MATRIX* func_800B396C(MATRIX*, VECTOR*);
MATRIX* pc_test_comp_matrix(MATRIX*, MATRIX*) __asm__("CompMatrixLV");
short D_80040C18[192] = { [0] = 4096, [64] = 5792 };
int vs_gte_rsqrt(int);
int func_800E4660(int, int, int);
void pc_room_transform(VECTOR*, VECTOR*, MATRIX*, MATRIX*);
void pc_scaled_room_transform(VECTOR*, VECTOR*, MATRIX*, int, MATRIX*);
unsigned* pc_background_packets(unsigned*, unsigned*, unsigned*, int[34][4]);
#ifdef PC_ROOM_TEST
unsigned pc_project_bone_vertices(
    unsigned, const unsigned*, const SVECTOR*, MATRIX*, unsigned*, unsigned*);
unsigned* pc_room_group_packets(
    unsigned*, unsigned*, unsigned*, unsigned, unsigned, unsigned*);
int func_800DC284(int, int, int, int);
int func_800E437C(short*, short*, int, unsigned);
unsigned char _trig_table[0x1004];
#endif
static int test_ticks;
int pc_window_enabled;
unsigned pc_window_buttons(void) { return 0; }
void pc_tick(void) { ++test_ticks; }
int func_800E45B4(void) { return 17; }
static unsigned copied_packet[5];
void pc_draw_packet(void* p, int chain)
{
    if (chain)
        exit(1);
    for (int i = 0; i < 5; ++i)
        copied_packet[i] = ((unsigned*)p)[i];
}
int main(void)
{
    MATRIX identity = { { { 4096, 0, 0 }, { 0, 4096, 0 }, { 0, 0, 4096 } }, { 0, 0, 0 } };
    SVECTOR v = { 100, 50, 1000, 0 };
    VECTOR out;
    long flag, p;
    int xy;
    CHECK(VSync(-1) == 0);
    CHECK(VSync(1) == 17 && test_ticks == 0);
    CHECK(VSync(0) == 17 && test_ticks == 1);
    CHECK(VSync(2) == 17 && test_ticks == 3 && VSync(-1) == 3);
    RECT copy_rect = { 12, 34, 8, 16 };
    CHECK(MoveImage(&copy_rect, 56, 78) == 0);
    CHECK(copied_packet[0] == 0x04ffffff && copied_packet[1] == 0x80000000);
    CHECK(copied_packet[2] == (12u | (34u << 16))
          && copied_packet[3] == (56u | (78u << 16)));
    CHECK(copied_packet[4] == (8u | (16u << 16)));
    InitGeom();
    CHECK(CFC2(29) == 341 && CFC2(30) == 256);
    CHECK(CFC2(27) == (unsigned int)-0x1062);
    SetRotMatrix(&identity);
    SetTransMatrix(&identity);
    RotTrans(&v, &out, &flag);
    CHECK(out.vx == 100 && out.vy == 50 && out.vz == 1000);
    SetGeomOffset(100, 50);
    SetGeomScreen(1000);
    CHECK(RotTransPers(&v, (long*)&xy, &p, &flag) == 250);
    CHECK((short)xy == 200 && (short)(xy >> 16) == 100);
    MTC2(0, 12);
    MTC2(10, 13);
    MTC2(10u << 16, 14);
    gte_nclip();
    CHECK(MFC2(24) == 100);
    MTC2(1000, 17);
    MTC2(2000, 18);
    MTC2(3000, 19);
    gte_avsz3();
    CHECK(MFC2(7) == 499);
    CHECK(func_800E4660(3, 4, 12) == 169);
    CHECK(func_800E4660(65535, 65536, 0) == 1);
    VECTOR normal = { 4096, 0, 0, 0 };
    CHECK(VectorNormal(&normal, &normal) == 16777216);
    CHECK(normal.vx == 4096 && normal.vy == 0 && normal.vz == 0);
    normal = (VECTOR) { -2048, 2048, 0, 0 };
    CHECK(VectorNormal(&normal, &normal) == 8388608);
    CHECK(normal.vx == -2896 && normal.vy == 2896 && normal.vz == 0);
    SVECTOR short_normal = { -2048, 2048, 0, 77 };
    CHECK(VectorNormalSS(&short_normal, &short_normal) == 8388608);
    CHECK(short_normal.vx == -2896 && short_normal.vy == 2896 && short_normal.vz == 0
        && short_normal.pad == 77);
    MATRIX rotation = { { { 0, -4096, 0 }, { 4096, 0, 0 }, { 0, 0, 4096 } },
        { 123, 456, 789 } };
    MATRIX target = identity;
    target.t[0] = 77;
    CHECK(func_80041C68(&rotation, &target) == &target);
    CHECK(target.m[0][1] == -4096 && target.m[1][0] == 4096 && target.m[2][2] == 4096);
    CHECK(target.t[0] == 77);
    pc_test_comp_matrix(&target, &rotation);
    CHECK(target.m[0][0] == -4096 && target.m[1][1] == -4096 && target.m[2][2] == 4096);
    CHECK(target.t[0] == 77);
    SVECTOR angles = { 0, 0, 0, 0 };
    RotMatrixYXZ_gte(&angles, &target);
    CHECK(target.m[0][0] == 4096 && target.m[1][1] == 4096 && target.m[2][2] == 4096);
    CHECK(target.t[0] == 77);
    angles.vx = 1024;
    RotMatrixYXZ_gte(&angles, &target);
    CHECK(target.m[1][2] == -4096 && target.m[2][1] == 4096);
    CHECK(target.m[1][1] == 0 && target.m[2][2] == 0);
    angles = (SVECTOR) { 0, 1024, 0, 0 };
    func_800B17F0(&angles, &target);
    CHECK(target.m[0][2] == 4096 && target.m[2][0] == -4096 && target.m[1][1] == 4096);
    RotMatrix_gte(&angles, &target);
    CHECK(target.m[0][2] == 4096 && target.m[2][0] == -4096 && target.m[1][1] == 4096);
    angles = (SVECTOR) { 1024, 0, 1024, 0 };
    RotMatrix_gte(&angles, &target);
    CHECK(target.m[0][1] == -4096 && target.m[1][2] == -4096 && target.m[2][0] == 4096);
    CHECK(target.m[0][0] == 0 && target.m[1][1] == 0 && target.m[2][2] == 0);
    CHECK(target.t[0] == 77);
    angles = (SVECTOR) { 0, 1024, 0, 0 };
    func_800B17F0(&angles, &target);
    VECTOR scale = { 2048, 4096, 8192, 0 };
    func_800B396C(&target, &scale);
    CHECK(target.m[0][2] == 8192 && target.m[2][0] == -2048 && target.m[1][1] == 4096);
    CHECK(target.t[0] == 77);
    CHECK(SquareRoot12(0) == 0 && SquareRoot12(1) == 64 && SquareRoot12(4096) == 4096);
    CHECK(vs_gte_rsqrt(0) == 0 && vs_gte_rsqrt(1) == 1 && vs_gte_rsqrt(4096) == 64);
    VECTOR axis_x = { 4096, 0, 0, 0 }, axis_y = { 0, 4096, 0, 0 };
    unsigned old0 = CFC2(0), old2 = CFC2(2), old4 = CFC2(4);
    OuterProduct0(&axis_x, &axis_y, &out);
    CHECK(out.vx == 0 && out.vy == 0 && out.vz == 4096);
    CHECK(CFC2(0) == old0 && CFC2(2) == old2 && CFC2(4) == old4);
    VECTOR position = { 100, -20, 1000, 0 }, room_angles = { 0, 0, 0, 0 };
    MATRIX camera = identity, translation = identity;
    translation.t[0] = 7;
    translation.t[1] = 8;
    translation.t[2] = 9;
    camera.t[0] = 999;
    SetTransMatrix(&translation);
    pc_room_transform(&position, &room_angles, &camera, &target);
    CHECK(target.m[0][0] == 4096 && target.m[1][1] == 4096 && target.m[2][2] == 4096);
    CHECK(target.t[0] == 107 && target.t[1] == -12 && target.t[2] == 1009);
    camera = rotation;
    room_angles.vz = 1024;
    pc_room_transform(&position, &room_angles, &camera, &target);
    CHECK(target.m[0][0] == -4096 && target.m[1][1] == -4096 && target.m[2][2] == 4096);
    CHECK(target.t[0] == 27 && target.t[1] == 108 && target.t[2] == 1009);
    pc_scaled_room_transform(&position, &room_angles, &camera, 8192, &target);
    CHECK(target.m[0][0] == -2048 && target.m[1][1] == -2048 && target.m[2][2] == 2048);
    CHECK(target.t[0] == 17 && target.t[1] == 58 && target.t[2] == 509);
    pc_scaled_room_transform(&position, &room_angles, &camera, 4096, &target);
    CHECK(target.m[0][0] == -4096 && target.t[0] == 27 && target.t[1] == 108);
    unsigned mesh[34 * 3 + 64] = { 0 }, packets[64 * 7], head = 0x00ffffff;
    int projected[34][4];
    mesh[0] = (unsigned short)-10 | ((unsigned)(unsigned short)-10 << 16);
    mesh[1] = 100;
    mesh[2] = 0x300000ff;
    mesh[3] = 10 | ((unsigned)(unsigned short)-10 << 16);
    mesh[4] = 100;
    mesh[5] = 0x0000ff00;
    mesh[6] = 10u << 16;
    mesh[7] = 100;
    mesh[8] = 0x00ff0000;
    for (int i = 0; i < 64; ++i)
        mesh[102 + i] = 0x020100;
    SetRotMatrix(&identity);
    SetTransMatrix(&identity);
    CHECK(pc_background_packets(mesh, packets, &head, projected) == packets + 64 * 7);
    CHECK(packets[0] == 0x06ffffff && packets[1] == 0x300000ff);
    CHECK(packets[2] == (150 | (110u << 16)));
    CHECK((head & 0xffffff) == ((unsigned)(packets + 63 * 7) & 0xffffff));
    mesh[1] = mesh[4] = mesh[7] = (unsigned short)-100;
    head = 0x00ffffff;
    CHECK(pc_background_packets(mesh, packets, &head, projected) == packets);
    CHECK(head == 0x00ffffff);
#ifdef PC_ROOM_TEST
    MATRIX bones[2] = { identity, identity };
    bones[1].t[0] = 100;
    SVECTOR vertices[6] = { { 100, 50, 1000, 0 }, { 0, 0, 1000, 0 },
        { -100, -50, 1000, 0 }, { 0, 0, 1000, 0 }, { 100, 0, 1000, 0 },
        { 0, 0, -100, 0 } };
    unsigned groups[3] = { 3u << 16, (5u << 16) | 1, 6u << 16 };
    unsigned screen[6], depths[6];
    SetGeomOffset(100, 50);
    SetGeomScreen(1000);
    CHECK(pc_project_bone_vertices(3, groups, vertices, bones, screen, depths) == 1000);
    CHECK(screen[0] == (200u | (100u << 16)) && screen[1] == (100u | (50u << 16)));
    CHECK(screen[2] == 0 && screen[3] == (200u | (50u << 16)));
    CHECK(screen[4] == (300u | (50u << 16)));
    CHECK(depths[0] == 1000 && depths[4] == 1000 && depths[5] == 0);
    CHECK(pc_project_bone_vertices(0, groups, vertices, bones, screen, depths) == 0);
    SetGeomOffset(100, 50);
    unsigned room[20] = { 1, 1 }, output[26], ot[2048];
    for (int i = 0; i < 2048; ++i)
        ot[i] = 0xffffff;
    unsigned* poly = room + 2;
    for (int i = 0; i < 2; ++i) {
        poly[0] = (unsigned short)-32 | ((unsigned)(unsigned short)-32 << 16);
        poly[1] = 1024 | (64u << 16);
        poly[2] = 64u << 16;
        poly[3] = i ? 0x3c808080 : 0x34808080;
        poly[4] = poly[5] = 0x00808080;
        poly[6] = 0x12340000;
        poly[7] = 0x014a0040;
        if (i) {
            poly[8] = 0x40004040;
            poly[9] = 0x40808080;
        }
        poly += 8;
    }
    SetRotMatrix(&identity);
    SetTransMatrix(&identity);
    SetGeomOffset(160, 112);
    SetGeomScreen(1024);
    CHECK(pc_room_group_packets(room, output, ot, 0, 0, output + 26) == output + 23);
    CHECK(output[0] == 0x09ffffff && output[1] == 0x34808080);
    CHECK(output[2] == (128 | (80u << 16)) && output[3] == 0x12340000);
    CHECK(output[10] >> 24 == 12
          && (ot[256] & 0xffffff) == ((unsigned)(output + 10) & 0xffffff));
    CHECK(output[10 + 11] == (192 | (144u << 16)));
    /* Adaptive subdivision: level (gp >> 13 & 3) - (depth >> 9) = 1 gives
     * four midpoint triangles, then three depth-zero edge triangles. */
    unsigned split[72];
    room[1] = 0;
    for (int i = 0; i < 2048; ++i)
        ot[i] = 0xffffff;
    CHECK(pc_room_group_packets(room, split, ot, 0x6000, 1, split + 72) == split + 70);
    CHECK(split[0] == 0x09ffffff && split[1] == 0x34808080);
    CHECK(split[2] == (128 | (80u << 16)) && split[5] == (160 | (80u << 16))
          && split[8] == (128 | (112u << 16)));
    CHECK(split[3] == 0x12340000 && split[6] == 0x014a0020 && split[9] == 0);
    CHECK(split[4] == 0x00808080);
    CHECK(split[42] == (128 | (144u << 16)) && split[62] == (160 | (80u << 16)));
    CHECK((split[60] & 0xffffff) == ((unsigned)(split + 50) & 0xffffff));
    CHECK((ot[256] & 0xffffff) == ((unsigned)(split + 60) & 0xffffff));
    /* MVMVA weighted sum with signed 16-bit inputs and its matrix side effect. */
    CHECK(func_800DC284(3, 10, 0x10007, -5) == 18 + 40 + 7 - 10);
    CHECK(func_800DC284(-1, -2, 3, 0x20004) == -6 - 8 + 3 + 8);
    CHECK(CFC2(0) == (0x30000u | 0xfffe) && CFC2(1) == 0x20004 && CFC2(2) == 0);
    /* Facing cosine: zero distance, exact quadrant values and a table lookup. */
    short eye[3] = { 0, 0, 0 }, ahead[3] = { 4096, 0, 0 };
    *(unsigned short*)(_trig_table + 0x100 * 4 + 2) = 0x0b50;
    CHECK(func_800E437C(eye, ahead, 0x400, 0) == 0x1000);
    CHECK(func_800E437C(eye, ahead, 0xc00, 1) == 0x1000);
    CHECK(func_800E437C(eye, ahead, 0x400, 1) == -0x1000);
    CHECK(func_800E437C(eye, ahead, 0xb00, 1) == 0x0b50);
#endif
    puts(
        "PASS: original GTE defaults, identity transform, perspective projection, NCLIP, AVSZ3, normalization");
    return 0;
}
