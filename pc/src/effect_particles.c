/* Native adaptation of the retained gte-macros/s5.c decompilation candidate
 * for BATTLE/6E644.s. Preserves original packet layout and GTE stages, but is
 * not a matching decompilation claim. Uses an explicit host argument instead
 * of the original wrapper's implicit MIPS argument-register forwarding. */
#include "common.h"
#include "src/BATTLE/BATTLE.PRG/5BF94.h"
#include "src/BATTLE/BATTLE.PRG/6E644.h"
#include "src/SLUS_010.40/main.h"
#include <inline_c.h>
extern void pc_unimplemented(const char*);
#define gte_ctc2(reg, value) CTC2((value), (reg))
#define gte_mtc2(reg, value) MTC2((value), (reg))
#define gte_mfc2(reg, value) ((value) = (int)MFC2(reg))
#define gte_mfc2_b(reg, value) gte_mfc2(reg, value)
#define gte_lwc2(reg, ptr) MTC2(*(unsigned*)(ptr), (reg))
#define gte_mvmva2_b(sf, mx, v, cv, lm) gte_mvmva(sf, mx, v, cv, lm)
#define gte_rtv0tr2() gte_rtv0tr()
#define gte_rtps2() gte_rtps()
static unsigned pc_curve_value(const u_char* sizes, unsigned index, unsigned clock)
{
    if (index >= 32 || sizes[index] == 0)
        pc_unimplemented("invalid particle color curve");
    return psx_get(D_800F569C->curves[index])[clock % sizes[index]];
}

typedef struct func_800D6E44_t {
    psx_ptr(struct func_800D6E44_t) previous;
    psx_ptr(struct func_800D6E44_t) next;
    short unk8;
    short unkA;
    int x;
    int y;
    int z;
    int dx;
    int dy;
    int dz;
    int unk24[3];
    int unk30[3];
    func_800D6CF_t sprite;
    int flags;
    union {
        u_int value;
        struct {
            u_int scale : 8;
            u_int depthMode : 4;
            u_int unk60_12 : 4;
            u_int unk62 : 16;
        } bits;
    } unk60;
    short unk64[2];
    short lifetime;
    u_char matrixIndex;
    u_char unk6B;
    u_char endEvent;
    u_char tickEvent;
    char unk6E[2];
    int unk70;
    func_800D2904_curves curves;
} func_800D6E44_t;

/* A host view of the game's particle node (func_800D2904_t, 5BF94.h, PS1
 * layout with psx_ptr links), whose int unk3C[8] holds the sprite state
 * (func_800D6CF_t, also PS1 layout): the two views must agree on every target. */
_Static_assert(sizeof(func_800D6CF_t) <= sizeof(((func_800D2904_t*)0)->unk3C)
                   && __builtin_offsetof(func_800D6E44_t, sprite)
                          == __builtin_offsetof(func_800D2904_t, unk3C)
                   && __builtin_offsetof(func_800D6E44_t, curves)
                          == __builtin_offsetof(func_800D2904_t, unk74)
                   && sizeof(func_800D6E44_t) == sizeof(func_800D2904_t),
               "particle view != func_800D2904_t");
PSX_ASSERT_SIZE(func_800D6E44_t, 0x78);
#ifndef ROOD_PC_LP64
PSX_ASSERT_OFFSET(func_800D6E44_t, sprite, 0x3C);
PSX_ASSERT_OFFSET(func_800D6E44_t, curves, 0x74);
#endif

extern u_char D_80040A14[];
/* Native particles (effect_native.c; absent in unit probes). */
extern void pc_particles_native_begin(void) __attribute__((weak));
extern void pc_particles_native_node(unsigned* start, unsigned* end, int tiles, const int view[3], int sz,
    unsigned xy, int bucket, int layered, const void* node, int age, unsigned source) __attribute__((weak));
extern unsigned* pc_particles_native_end(unsigned* packet, unsigned* ot) __attribute__((weak));

void func_800D6E44(void* head)
{
    func_800D6E44_t* node = head;
    u_char* curveSizes = psx_get(D_800F569C->curveBlock)->curveSizes;
    int projection = vs_main_projectionDistance << 10;
    D_800F53B8_t* parent = D_800F53BC;
    int x, y, z, a, b, sinv, otz, depth, mode, rgb, clock, index, size;
    MATRIX* m;
    pFileBlock1Indices* indices;
    pFileBlock1Data* tile;
    u_long* packet;

    gte_ctc2(11, 0);
    gte_ctc2(12, 0x1000);
    if (pc_particles_native_begin)
        pc_particles_native_begin();

    for (; node != NULL; node = psx_get(node->next)) {
        if (node->sprite.unk6 != 0) {
            node->sprite.unk6 -= 2;
            if (node->sprite.unk6 != 0) {
                goto scriptDone;
            }
        }
        while (1) {
            int cmd = *(int*)(psx_get(node->sprite.unk18) + node->sprite.unkE);
            switch ((unsigned)cmd & 0xF0000000) {
            case 0:
                psx_set(node->sprite.unk1C, psx_get(D_800F569C->block1Tables[node->sprite.unk16]) + (cmd & 0xFFFF));
                *(short*)&node->sprite.unk6 = (cmd >> 16) & 0x3FFF;
                node->sprite.unkE += 4;
                goto scriptDone;
            case 0x10000000:
                node->sprite.unk8 = (cmd << 18) >> 18;
                node->sprite.unkA = (cmd << 4) >> 18;
                node->sprite.unkE += 4;
                break;
            case 0x20000000:
                node->sprite.unkE = 0;
                break;
            case 0x30000000:
                goto scriptDone;
            default:
                pc_unimplemented("unknown particle sprite command");
            }
        }
    scriptDone:
        x = (node->x >> 12) + node->sprite.unk8;
        y = (node->y >> 12) + node->sprite.unkA;
        z = node->z >> 12;
        a = (x & 0xFFFF) + (y << 16);
        if (node->flags & 0x20000) {
            if (node->flags & 0x80000) {
                m = &parent->unk1C[node->matrixIndex].unk38;
                gte_SetRotMatrix(m);
                gte_SetTransMatrix(m);
                b = z;
                gte_mtc2(0, a);
                gte_mtc2(1, b);
                gte_rtv0tr2();
                gte_mfc2(26, b);
                if (b > 0) {
                    continue;
                }
            }
            m = &parent->unk1C[node->matrixIndex].unk58;
            gte_SetRotMatrix(m);
            gte_SetTransMatrix(m);
        } else {
            if ((node->flags & 0x80000) && (y > 0)) {
                continue;
            }
            gte_SetRotMatrix(&vs_scratch.viewMatrix);
            gte_SetTransMatrix(&vs_scratch.viewMatrix);
        }
        b = z;
        gte_mtc2(0, a);
        gte_mtc2(1, b);
        gte_rtps2();
        gte_mfc2_b(14, a);
        gte_mfc2_b(19, otz);
        /* The centre in view space and its depth (native particles). */
        int view[3] = { (int)MFC2(25), (int)MFC2(26), (int)MFC2(27) }, centreSz = otz;
        unsigned centreXy = (unsigned)a;
        otz >>= 2;
        if (node->unk60.value & 0xF00) {
            mode = (node->unk60.value >> 8) & 0xF;
        } else {
            mode = node->sprite.unk7;
        }
        depth = otz;
        switch (mode) {
        case 0:
            break;
        case 3:
            depth -= 8;
            break;
        case 4:
            depth -= 16;
            break;
        case 1:
            depth += 8;
            break;
        case 2:
            depth += 16;
            break;
        }
        if (depth >= 0x800 || depth <= vs_main_nearClip) {
            continue;
        }
        switch (mode) {
        case 5:
            depth = 0x7FE;
            break;
        case 6:
            depth = -15;
            break;
        case 7:
            depth = -13;
            break;
        case 8:
            depth = -11;
            break;
        }
        packet = psx_get(vs_scratch.unk0);
        u_long* nodeStart = packet;
        psx_set(vs_scratch.unk0, packet + 3);
        packet[0] = (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFFFFFF) | 0x1000000;
        ((u_long*)psx_get(vs_scratch.unk4))[depth] =
            (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFF000000)
            | (psx_to_int(u_long, packet) & 0xFFFFFF);
        packet[1] = 0xE1000200;
        packet[2] = 0;
        if (node->flags & 0x10000) {
            int t, ax, ay, c;
            x = ((x + (node->dx >> 5)) & 0xFFFF) + ((y + (node->dy >> 5)) << 16);
            z += node->dz >> 5;
            gte_mtc2(0, x);
            gte_mtc2(1, z);
            gte_rtps2();
            gte_mfc2_b(14, x);
            y = (short)a - (short)x;
            z = (a >> 16) - (x >> 16);
            c = 0;
            t = z | y;
            if (t != 0) {
                ax = y < 0 ? -y : y;
                ay = z < 0 ? -z : z;
                t = 512;
                if (ax < ay) {
                    c = ax << 9;
                    c /= ay;
                    c = D_80040A14[c];
                    c = t - c;
                } else {
                    c = ay << 9;
                    c /= ax;
                    c = D_80040A14[c];
                }
                t = 1024;
                if (ax != y) {
                    c = t - c;
                }
                c <<= 1;
                if (ay != z) {
                    c = -c;
                }
            }
            node->sprite.unk14 = c;
        }
        if (otz == 0)
            pc_unimplemented("effect particle projection depth zero");
        int projectionScale = projection / otz;
        gte_ctc2(13, (short)a);
        a >>= 16;
        gte_ctc2(14, a);
        gte_ctc2(15, 0);
        indices = psx_get(node->sprite.unk1C);
        tile = psx_get(D_800F569C->block1Data) + indices->dataIndex;
        a = projectionScale;
        b = a * ((rcos(node->sprite.unk14) * node->unk60.bits.scale) / 4);
        sinv = (rsin(node->sprite.unk14) * node->unk60.bits.scale) / 4;
        b >>= 12;
        b &= 0xFFFF;
        sinv *= a;
        sinv >>= 12;
        a = b | (-sinv << 16);
        gte_ctc2(8, a);
        sinv <<= 16;
        gte_ctc2(9, sinv);
        gte_ctc2(10, b);
        rgb = 0;
        clock = (u_char)(node->curves.fields.age + 1);
        if (node->curves.fields.rCurve) {
            index = node->curves.fields.rCurve - 1;
            index = pc_curve_value(curveSizes, index, clock);
            if (index) {
                rgb = index * 2 - 1;
            }
        } else {
            rgb = 0x80;
        }
        if (node->curves.fields.gCurve) {
            index = node->curves.fields.gCurve - 1;
            index = pc_curve_value(curveSizes, index, clock);
            if (index) {
                rgb |= (index << 9) - 0x100;
            }
        } else {
            rgb |= 0x8000;
        }
        if (node->curves.fields.bCurve) {
            index = node->curves.fields.bCurve - 1;
            index = pc_curve_value(curveSizes, index, clock);
            if (index) {
                rgb |= (index * 2 - 1) << 16;
            }
        } else {
            rgb |= 0x800000;
        }
        for (index = 0; index < indices->unk0; ++index) {
            packet = psx_get(vs_scratch.unk0);
            gte_lwc2(0, &tile->unk8[0]);
            gte_lwc2(2, &tile->unk8[4]);
            gte_lwc2(4, &tile->unk8[8]);
            y = *(int*)tile;
            z = *(u_short*)&tile->u0;
            size = *(u_short*)&tile->u1;
            gte_mvmva2_b(1, 1, 0, 1, 0);
            *(u_short*)&packet[7] = z + (size & 0xFF00);
            packet[5] = (z + (size & 0xFF)) | ((y >> 16) << 16);
            gte_mfc2_b(9, a);
            gte_mfc2_b(10, b);
            packet[2] = (a & 0xFFFF) + (b << 16);
            gte_mvmva2_b(1, 1, 1, 1, 0);
            if (y & 0x8000) {
                packet[1] = rgb | 0x2E000000;
            } else {
                packet[1] = rgb | 0x2C000000;
            }
            *(u_short*)&packet[9] = z + size;
            gte_mfc2_b(9, a);
            gte_mfc2_b(10, b);
            packet[4] = (a & 0xFFFF) + (b << 16);
            gte_mvmva2_b(1, 1, 2, 1, 0);
            psx_set(vs_scratch.unk0, (u_long*)psx_get(vs_scratch.unk0) + 10);
            packet[0] = (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFFFFFF) | 0x9000000;
            gte_mfc2_b(9, a);
            gte_mfc2_b(10, b);
            packet[6] = (a & 0xFFFF) + (b << 16);
            gte_lwc2(0, &tile->unk8[12]);
            packet[3] = (z & 0xFFFF) | (y << 16);
            gte_mvmva2_b(1, 1, 0, 1, 0);
            ++tile;
            ((u_long*)psx_get(vs_scratch.unk4))[depth] =
                (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFF000000)
                | (psx_to_int(u_long, packet) & 0xFFFFFF);
            gte_mfc2_b(9, a);
            gte_mfc2_b(10, b);
            packet[8] = (a & 0xFFFF) + (b << 16);
        }
        packet = psx_get(vs_scratch.unk0);
        psx_set(vs_scratch.unk0, packet + 3);
        packet[0] = (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFFFFFF) | 0x1000000;
        ((u_long*)psx_get(vs_scratch.unk4))[depth] =
            (((u_long*)psx_get(vs_scratch.unk4))[depth] & 0xFF000000)
            | (psx_to_int(u_long, packet) & 0xFFFFFF);
        packet[1] = 0xE1000000;
        packet[2] = 0;
        if (pc_particles_native_node) {
            /* The node's particle system for glows (glows.c): its sprite
             * script and table, and the matrix it is attached to. */
            unsigned source = (unsigned)psx_raw(node->sprite.unk18) * 2654435761u
                            ^ ((unsigned)node->sprite.unk16 << 8)
                            ^ (node->flags & 0x20000 ? 0x10000u + node->matrixIndex : 0);
            pc_particles_native_node((unsigned*)nodeStart, (unsigned*)psx_get(vs_scratch.unk0), indices->unk0, view,
                centreSz, centreXy, depth, mode >= 5 && mode <= 8, node, node->curves.fields.age,
                (source & 0x07ffffffu) | (node->flags & 0x20000 ? 0x08000000u : 0) | 1); /* attached: glows.c */
        }
    }
    if (pc_particles_native_end)
        psx_set(vs_scratch.unk0, (u_long*)pc_particles_native_end((unsigned*)psx_get(vs_scratch.unk0),
                                     (unsigned*)psx_get(vs_scratch.unk4)));
}
