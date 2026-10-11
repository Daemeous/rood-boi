/* Native equivalent of hand-written BATTLE/BATTLE.PRG/5A624.s func_800C2E24:
 * the area Map screen renderer called by func_800C4794. Not a matching
 * decompilation claim; it reproduces the original memory effects literally.
 *
 * Scratchpad work space (all left behind as the original leaves it):
 *   0x1F800088  9x9 cell heights for the current view rotation (+3 pad bytes)
 *   0x1F8000DC  per-scanline span counts (224 rows)
 *   0x1F8001BC  180 cell-edge flags, then their row bytes at +0xB4; later
 *               reused for triangle span rows and the 32-point view circle
 * D_800EB9B8 + 0xB70 holds 224 rows of 32 x-coordinates (span pairs). The
 * spans are built from the projected cell outlines, cut by lowered walls and
 * marker triangles (+0x4AB8), clipped to the circle's left edge, then emitted
 * as semi-transparent LINE_F2 packets along the circle's right edge. */
#include "common.h"
#include <libgte.h>
#include <inline_c.h>

extern void pc_unimplemented(const char*);
/* A 32-bit guest word: the battle menu state block's address. */
extern u_int D_800EB9B8;
extern u_short D_800EA130[]; /* 0x8000 / n; indexed past its end as the original */
extern u_short D_800EA6B0[]; /* edge -> pair of neighbouring cell indices */
extern short D_800EA818[];   /* 40-entry sine table: sin at i, cos at i + 8 */

#define PC_MAP_SCRATCH(off) ((u_char*)0x1f800000 + (off))

static int pc_map_recip(int dy)
{
    return *(u_short*)((u_char*)D_800EA130 + dy * 2);
}

/* Fixed-point x step per scanline: (delta >> 15) * D_800EA130[dy]. */
static int pc_map_step(int delta, int dy)
{
    return (delta >> 15) * pc_map_recip(dy);
}

static unsigned pc_map_rtps(unsigned xy, int z)
{
    MTC2(xy, 0);
    MTC2(z, 1);
    gte_rtps();
    return MFC2(14);
}

/* Triangle/circle row buffers live after the edge flags; the PS1 scratchpad
 * ends at 0x1F800400 and the original never checks. */
static short* pc_map_row(int offset)
{
    if (offset < 0 || 0x1bc + offset + 2 > 0x400)
        pc_unimplemented("map row buffer beyond scratchpad");
    return (short*)PC_MAP_SCRATCH(0x1bc + offset);
}

/* 5A624.s 9x9 cell pass: unreachable/too-high cells become 0, floors visible
 * from height y get min(y - floor, 0x30), others keep the preset 1. */
static void pc_map_cell(u_char* out, unsigned cell, int y)
{
    int low = (int)(cell << 15) >> 17;
    int high = (int)cell >> 17;
    if (y < low) {
        if (high == 0 || high < y)
            *out = 0;
    } else if (cell & 1) {
        int h = y - low;
        if (h >= 0x31)
            h = 0x30;
        *out = h;
    }
}

/* Remove [left, right] from the first span of a row it touches (cases at
 * 0x800C3690 and 0x800C3F58): drop, trim either end, or split the span. */
static void pc_map_cut(u_char* count, short* row, int left, int right)
{
    int i = 0, j;
    if (*count == 0)
        return;
    for (;;) {
        int x1 = row[i + 1];
        if (left < x1) {
            int x0 = row[i];
            if (!(x0 < right))
                return;
            switch ((x0 < left) + ((right < x1) ? 2 : 0)) {
            case 0:
                *count -= 2;
                for (j = i; j < *count; ++j)
                    row[j] = row[j + 2];
                return;
            case 1:
                row[i + 1] = left;
                return;
            case 2:
                row[i] = right;
                return;
            case 3:
                for (j = *count - 1; i < j; --j)
                    row[j + 2] = row[j];
                *count += 2;
                row[j + 1] = left;
                row[j + 2] = right;
                return;
            }
        }
        i += 2;
        if (!(i < *count))
            return;
    }
}

/* Wall cuts against a lowered edge at x = p with wall limit lim.
 * mode 0 (0x800C389C/0x800C39A4): [x0,x1] -> [x0,lim][p,x1] or [p,x1].
 * mode 1 (0x800C3AA8): [x0,x1] -> [x0,p][lim,x1] or [x0,p], needs x0 < p.
 * mode 2 (0x800C3BAC): as mode 1 but accepts x0 == p. */
static void pc_map_cut_wall(u_char* count, short* row, int p, int lim, int mode)
{
    int i = 0, j;
    if (*count == 0)
        return;
    for (;;) {
        int x1 = row[i + 1];
        if (p < x1) {
            int x0 = row[i];
            if (mode == 2 ? p < x0 : !(x0 < p))
                return;
            if (mode == 0) {
                if (!(x0 < lim)) {
                    row[i] = p;
                    return;
                }
            } else if (!(lim < x1)) {
                row[i + 1] = p;
                return;
            }
            for (j = *count - 1; i < j; --j)
                row[j + 2] = row[j];
            *count += 2;
            if (mode == 0) {
                row[j + 1] = lim;
                row[i + 2] = p;
            } else {
                row[j + 2] = lim;
                row[i + 1] = p;
            }
            return;
        }
        i += 2;
        if (!(i < *count))
            return;
    }
}

void func_800C2E24(SVECTOR* origin, MATRIX* m, int scale)
{
    u_char* state = psx_to_ptr(u_char*, D_800EB9B8);
    u_char* grid = PC_MAP_SCRATCH(0x88);
    u_char* rows = PC_MAP_SCRATCH(0xdc);
    u_char* edges = PC_MAP_SCRATCH(0x1bc);
    unsigned* cells = (unsigned*)(state + 0x4370);
    short* spans = (short*)(state + 0xb70);
    u_short* outline;
    unsigned* points = (unsigned*)edges;
    unsigned* packet;
    unsigned yHi;
    int y = origin->vy;
    int baseX, baseZ, rotation, i, k, row, col, top, topIndex, index;

    for (i = 0x14; i >= 0; --i)
        ((unsigned*)grid)[i] = 0x01010101;
    grid[0x53] = 0;
    for (i = 0x15; i < 0x7a; ++i)
        ((unsigned*)grid)[i] = 0;

    k = 0;
    switch (((*(short*)(state + 0x22) - 0x200) >> 10) & 3) {
    case 0:
        for (row = 0; row < 0x49; row += 9)
            for (col = 0; col < 9; ++col)
                pc_map_cell(grid + k++, cells[col + row], y);
        break;
    case 1:
        for (col = 0; col < 9; ++col)
            for (row = 0x48; row >= 0; row -= 9)
                pc_map_cell(grid + k++, cells[col + row], y);
        break;
    case 2:
        for (row = 0x48; row >= 0; row -= 9)
            for (col = 8; col >= 0; --col)
                pc_map_cell(grid + k++, cells[col + row], y);
        break;
    case 3:
        for (col = 8; col >= 0; --col)
            for (row = 0; row < 0x49; row += 9)
                pc_map_cell(grid + k++, cells[col + row], y);
        break;
    }

    /* Toggle the four edges of every empty cell (19-byte edge rows). */
    k = 0;
    for (row = 0; row < 0x99; row += 0x13)
        for (col = 0; col < 9; ++col)
            if (grid[k++] == 0) {
                u_char* e = edges + col + row;
                e[0] = 1 - e[0];
                e[9] = 1 - e[9];
                e[10] = 1 - e[10];
                e[0x13] = 1 - e[0x13];
            }

    /* Project the remaining edges at height y and add them to the spans. */
    yHi = (unsigned)y << 16;
    baseX = (((short)origin->vx >> 7) - 4) << 7;
    baseZ = (((short)origin->vz >> 7) - 4) << 7;
    rotation = ((*(short*)(state + 0x22) - 0x200) >> 10) & 3;
    outline = (u_short*)(state + 0x44b4 + rotation * 0x168);
    for (i = 0; i < 0xb4; ++i) {
        u_char* e = edges + i;
        unsigned t0, xy2;
        int t1, t2, t3, t5, z2, r, step;
        if (*e == 0)
            continue;
        t0 = outline[i];
        t2 = pc_map_rtps(((baseX + (t0 & 0xf) * 128) & 0xffff) | yHi,
                         baseZ + ((t0 << 3) & 0x780));
        z2 = ((t0 >> 5) & 0x780) + baseZ;
        xy2 = ((baseX + ((t0 >> 1) & 0x780)) & 0xffff) | yHi;
        t1 = t2 >> 16;
        t2 = (unsigned)t2 << 16;
        t3 = pc_map_rtps(xy2, z2);
        *e = 0;
        t2 += 0x8000;
        t5 = t3 >> 16;
        t3 = ((unsigned)t3 << 16) | 0x8000;
        if ((unsigned)(t1 - t5 + 2) < 5) {
            *e = t3 < t2 ? 0xff : 0x7f;
            e[0xb4] = t1;
        }
        if (t1 < t5) {
            if (t5 <= 0 || !(t1 < 0xe0))
                continue;
            r = t1 < 0 ? 0 : t1;
            if (*e == 0) {
                int c = rows[r];
                if (t3 - t2 < 0 ? (c & 1) : ((c + 1) & 1))
                    *e = rows[r] + 1;
            }
            step = pc_map_step(t3 - t2, t5 - t1);
            if (t5 >= 0xe1)
                t5 = 0xe0;
            e[0xb4] = t5;
            while (t1 < 0) {
                ++t1;
                t2 += step;
            }
            do {
                u_char* c = rows + t1;
                int n = *c;
                int x = t2 >> 16;
                t2 += step;
                *c = n + 1;
                spans[t1 * 32 + n] = x;
                ++t1;
            } while (t1 < t5);
        } else if (t5 < t1) {
            if (t1 <= 0 || !(t5 < 0xe0))
                continue;
            r = t5 < 0 ? 0 : t5;
            if (*e == 0) {
                int c = rows[r];
                if (t2 - t3 < 0 ? (c & 1) : ((c + 1) & 1))
                    *e = rows[r] + 0x81;
            }
            step = pc_map_step(t2 - t3, t1 - t5);
            if (t1 >= 0xe1)
                t1 = 0xe0;
            e[0xb4] = t1;
            while (t5 < 0) {
                ++t5;
                t3 += step;
            }
            do {
                u_char* c = rows + t5;
                int n = *c;
                int x = t3 >> 16;
                t3 += step;
                *c = n + 1;
                spans[t5 * 32 + n] = x;
                ++t5;
            } while (t5 < t1);
        }
    }

    /* Walls: project inner edges lowered by the two neighbouring cell heights
     * and cut the spans between the lowered and the original edge. */
    for (i = 10; i < 0xaa; ++i) {
        u_char* e = edges + i;
        int a2 = *e;
        unsigned pair, t0, yLo, xy2;
        int t1, t2, t3, t5, t9, z2, step;
        if (a2 == 0)
            continue;
        pair = D_800EA6B0[i];
        t9 = grid[pair & 0xff] + grid[pair >> 8];
        if (t9 < 2)
            continue;
        t0 = outline[i];
        if (a2 & 0x80)
            t0 = ((t0 >> 8) | (t0 << 8)) & 0xffff;
        yLo = (unsigned)(y - t9) << 16;
        t2 = pc_map_rtps(((baseX + (t0 & 0xf) * 128) & 0xffff) | yLo,
                         baseZ + ((t0 << 3) & 0x780));
        z2 = ((t0 >> 5) & 0x780) + baseZ;
        xy2 = ((baseX + ((t0 >> 1) & 0x780)) & 0xffff) | yLo;
        t1 = t2 >> 16;
        t2 = (unsigned)t2 << 16;
        t3 = pc_map_rtps(xy2, z2);
        t9 = t2 >> 16;
        t2 += 0x8000;
        t5 = t3 >> 16;
        if (t5 <= 0)
            continue;
        t3 = ((unsigned)t3 << 16) | 0x8000;
        if ((a2 & 0x7f) == 0x7f) {
            t2 = t9;
            t5 = e[0xb4];
            t3 >>= 16;
            if (!(t5 < 0xe0))
                t5 = 0xe0;
            if (t1 < 0)
                t1 = 0;
            for (; t1 < t5; ++t1)
                pc_map_cut(rows + t1, spans + t1 * 32, t2, t3);
            continue;
        }
        step = pc_map_step(t3 - t2, t5 - t1); /* t5 - t1 may be negative */
        if (t5 >= 0xe1)
            t5 = 0xe0;
        while (t1 < 0) {
            ++t1;
            t2 += step;
        }
        if (a2 & 1) {
            for (; t1 < t5; ++t1, t2 += step)
                pc_map_cut_wall(rows + t1, spans + t1 * 32, t2 >> 16, t9, 0);
            t1 = e[0xb4];
            t2 >>= 16;
            for (; t5 < t1; ++t5)
                pc_map_cut_wall(rows + t5, spans + t5 * 32, t2, t9, 0);
        } else {
            for (; t1 < t5; ++t1, t2 += step)
                pc_map_cut_wall(rows + t1, spans + t1 * 32, t2 >> 16, t9, 1);
            t1 = e[0xb4];
            t2 >>= 16;
            for (; t5 < t1; ++t5)
                pc_map_cut_wall(rows + t5, spans + t5 * 32, t2, t9, 2);
        }
    }

    /* Marker triangles from the 9x9 grid at +0x4AB8: rasterise and cut. */
    for (row = 0; row < 9; ++row) {
        for (col = 0; col < 9; ++col) {
            int a2 = state[0x4ab8 + row * 9 + col];
            int bits, cx, cz, bottom, edge;
            if (a2 == 0)
                continue;
            bits = (int)0x38847780 >> (a2 & 31);
            cx = ((short)origin->vx >> 7) + col - 4;
            cz = ((short)origin->vz >> 7) + row - 4;
            MTC2((((cx + (bits & 1)) << 7) & 0xffff) | yHi, 0);
            MTC2((cz + ((bits >> 1) & 1)) << 7, 1);
            MTC2((((cx + ((bits >> 2) & 1)) << 7) & 0xffff) | yHi, 2);
            MTC2((cz + ((bits >> 3) & 1)) << 7, 3);
            MTC2((((cx + ((bits >> 4) & 1)) << 7) & 0xffff) | yHi, 4);
            MTC2((cz + ((bits >> 5) & 1)) << 7, 5);
            gte_rtpt();
            points[0] = MFC2(12);
            points[1] = MFC2(13);
            points[2] = MFC2(14);
            points[3] = points[0];
            top = (short)(points[0] >> 16);
            bottom = top;
            k = (short)(points[1] >> 16);
            if (k < top)
                top = k;
            if (bottom < k)
                bottom = k;
            k = (short)(points[2] >> 16);
            if (k < top)
                top = k;
            if (bottom < k)
                bottom = k;
            if (top < 0)
                top = 0;
            if (bottom >= 0xe1)
                bottom = 0xe0;
            for (edge = 0; edge < 3; ++edge) {
                int t1 = (int)points[edge] >> 16;
                int t2 = (points[edge] << 16) + 0x8000;
                int t3 = (points[edge + 1] << 16) + 0x8000;
                int t5 = (int)points[edge + 1] >> 16;
                int step;
                if (t1 < t5) {
                    step = pc_map_step(t3 - t2, t5 - t1);
                    if (t5 >= 0xe1)
                        t5 = 0xe0;
                    while (t1 < 0) {
                        ++t1;
                        t2 += step;
                    }
                    for (; t1 < t5; ++t1, t2 += step)
                        pc_map_row(0x12 + (t1 - top) * 4)[0] = t2 >> 16;
                } else {
                    step = pc_map_step(t2 - t3, t1 - t5);
                    if (t1 >= 0xe1)
                        t1 = 0xe0;
                    while (t5 < 0) {
                        ++t5;
                        t3 += step;
                    }
                    for (; t5 < t1; ++t5, t3 += step)
                        pc_map_row(0x10 + (t5 - top) * 4)[0] = t3 >> 16;
                }
            }
            for (k = top; k < bottom; ++k) {
                short* bounds = pc_map_row(0x10 + (k - top) * 4);
                pc_map_cut(rows + k, spans + k * 32, bounds[0], bounds[1]);
            }
        }
    }

    /* 32-point circle of radius `scale` around the origin, in camera space
     * (SXY, SZ >> 2 per point); remember the topmost on-screen point. */
    {
        int ax = (m->m[0][0] * scale + 0x800) >> 12;
        int az = (m->m[2][0] * scale + 0x800) >> 12;
        int bx = (m->m[0][2] * scale + 0x800) >> 12;
        int bz = (m->m[2][2] * scale + 0x800) >> 12;
        top = 0xe0;
        topIndex = 9; /* stale $s2 from the triangle loop when nothing is above row 0xE0 */
        for (i = 0; i < 32; ++i) {
            int c = D_800EA818[i + 8];
            int s = D_800EA818[i];
            int dx = (ax * c + az * s + 0x800) >> 12;
            int dz = (bx * c + bz * s + 0x800) >> 12;
            int sy;
            points[i * 2] = pc_map_rtps(((origin->vx + dx) & 0xffff) | yHi, origin->vz + dz);
            points[i * 2 + 1] = (int)MFC2(19) >> 2;
            sy = (short)(points[i * 2] >> 16);
            if (sy < top) {
                top = sy;
                topIndex = i;
            }
        }
    }

    /* Walk the circle's left side downwards, clipping span ends to it. */
    index = (topIndex - 1) & 31;
    for (;;) {
        int t2 = points[((index + 1) & 31) * 2];
        int t3 = points[index * 2];
        int t1 = t2 >> 16;
        int t5 = t3 >> 16;
        int step;
        if (t1 == t5 || t5 < 0) {
            if (index == topIndex)
                break;
            index = (index - 1) & 31;
            continue;
        }
        if (t5 < t1 || !(t1 < 0xe0))
            break;
        t2 = ((unsigned)t2 << 16) + 0x8000;
        t3 = ((unsigned)t3 << 16) + 0x8000;
        step = pc_map_step(t3 - t2, t5 - t1);
        if (t5 >= 0xe1)
            t5 = 0xe0;
        if (t1 < 0) { /* the original advances only one row here */
            ++t1;
            t2 += step;
        }
        if (t1 < -0xdc)
            pc_unimplemented("map circle clip above scratchpad");
        do {
            u_char* c = rows + t1;
            int n = *c;
            int x = t2 >> 16;
            if (n > 0) {
                short* line = spans + t1 * 32;
                for (;;) {
                    if (line[n - 2] < x) {
                        if (x < line[n - 1])
                            line[n - 1] = x;
                        break;
                    }
                    n -= 2;
                    if (n <= 0)
                        break;
                }
            }
            *c = n;
            ++t1;
            t2 += step;
        } while (t1 < t5);
        index = (index - 1) & 31;
    }

    /* Walk the right side downwards and emit the spans left of it. */
    packet = psx_load(unsigned*, 0x1f800000);
    index = (topIndex + 1) & 31;
    for (;;) {
        unsigned* p0 = points + ((index - 1) & 31) * 2;
        unsigned* p1 = points + index * 2;
        int t2 = p0[0];
        int t3 = p1[0];
        int t1 = t2 >> 16;
        int t5 = t3 >> 16;
        int z, zStep, step, recip;
        if (t1 == t5 || t5 < 0) {
            if (index == topIndex)
                break;
            index = (index + 1) & 31;
            continue;
        }
        if (t5 < t1 || !(t1 < 0xe0))
            break;
        z = ((unsigned)(short)p0[1] << 16) + 0x8000;
        zStep = ((unsigned)(short)p1[1] << 16) + 0x8000;
        recip = pc_map_recip(t5 - t1);
        zStep = ((zStep - z) >> 15) * recip;
        t2 = ((unsigned)t2 << 16) + 0x8000;
        t3 = ((unsigned)t3 << 16) + 0x8000;
        step = ((t3 - t2) >> 15) * recip;
        if (t5 >= 0xe1)
            t5 = 0xe0;
        while (t1 < 0) {
            ++t1;
            t2 += step;
            z += zStep;
        }
        do {
            int depth = z >> 16;
            if ((unsigned)depth < 0x800) {
                unsigned color = (unsigned)((t1 >> 1) << 8) | 0x42000000;
                unsigned* ot = psx_load(unsigned*, 0x1f800004) + depth;
                int x = t2 >> 16;
                int n = rows[t1];
                if (n > 0) {
                    short* line = spans + t1 * 32;
                    unsigned rowY = (unsigned)t1 << 16;
                    unsigned xy0 = (x & 0xffff) | rowY;
                    do {
                        unsigned w = *(unsigned*)(line + n - 2);
                        int x0 = (short)w;
                        int x1 = (int)w >> 16;
                        if (x0 < x1) {
                            unsigned from = (w & 0xffff) | rowY;
                            int last = 0;
                            if (x0 < x) {
                                if (!(x < x1))
                                    break;
                                from = xy0;
                                last = 1;
                            }
                            packet[0] = (*ot & 0xffffff) | 0x04000000;
                            packet[1] = 0xe1000220;
                            packet[2] = color;
                            packet[3] = from;
                            packet[4] = (w >> 16) | rowY;
                            *ot = psx_to_int(unsigned, packet) & 0xffffff;
                            packet += 5;
                            if (last)
                                break;
                        }
                        n -= 2;
                    } while (n > 0);
                }
            }
            ++t1;
            t2 += step;
            z += zStep;
        } while (t1 < t5);
        index = (index + 1) & 31;
    }
    psx_store(0x1f800000, packet);
}
