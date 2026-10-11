/* BATTLE/4B16C.s func_800B4594: per-vertex-colored character GT3/GT4 packets.
 * Keep the original packed records, material flags and depth bias. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void pc_unimplemented(const char*);
static unsigned actor_vertex(unsigned offset)
{
    if ((offset & 3) || offset >= 0xc00)
        pc_unimplemented("actor polygon vertex offset");
    return offset >> 2;
}
unsigned* pc_actor_packets(unsigned* packet, unsigned* ot, const unsigned* projected,
    const unsigned* depths, const unsigned* polygons, const unsigned short counts[3],
    unsigned tpage, unsigned clut)
{
    for (unsigned kind = 0; kind < 3; ++kind) {
        unsigned flags = kind == 2 && counts[kind] ? polygons[4] >> 24 : 0;
        for (unsigned face = 0; face < counts[kind]; ++face) {
            int quad = kind == 1 || (kind == 2 && (polygons[2] >> 24) == 0x3c);
            unsigned count = quad ? 4 : 3;
            unsigned index[4] = { actor_vertex(polygons[0] & 0xffff),
                actor_vertex(polygons[0] >> 16), actor_vertex(polygons[1] & 0xffff), 0 };
            if (quad)
                index[3] = actor_vertex(polygons[1] >> 16);
            int culled = 0;
            if (!(flags & 1)) {
                MTC2(projected[index[0]], 12);
                MTC2(projected[index[1]], 13);
                MTC2(projected[index[2]], 14);
                gte_nclip();
                culled = (int)MFC2(24) <= 0;
            }
            if (culled) {
                unsigned stride = kind == 2 ? polygons[3] >> 24 : (quad ? 32 : 24);
                if (stride < 24 || (stride & 3))
                    pc_unimplemented("actor polygon stride");
                polygons += stride / 4;
                /* The culled special-face branch reloads flags from word 0,
                 * whereas the emitted-face branch reloads word 4. */
                if (kind == 2 && face + 1 < counts[kind])
                    flags = polygons[0] >> 24;
                continue;
            }
            unsigned depth;
            if (quad) {
                depth = (depths[index[0]] + depths[index[1]] + depths[index[2]]
                            + depths[index[3]])
                     >> 4;
            } else {
                MTC2(depths[index[0]], 17);
                MTC2(depths[index[1]], 18);
                MTC2(depths[index[2]], 19);
                gte_avsz3();
                depth = MFC2(7);
            }
            if (kind == 2)
                depth += ((polygons[4] >> 26) & 3) * 4;
            if (depth < 0x7c0) {
                unsigned* head = ot + (int)depth - (kind == 2 ? 5 : 1);
                packet[0] = (*head & 0xffffff) | ((quad ? 12u : 9u) << 24);
                *head = (*head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
                unsigned uv01 = polygons[quad ? 6 : 5];
                unsigned uv23 = quad ? polygons[7] : polygons[1] >> 16;
                for (unsigned i = 0; i < count; ++i) {
                    packet[1 + i * 3] = polygons[2 + i];
                    packet[2 + i * 3] = projected[index[i]];
                }
                if (kind == 2)
                    packet[1] |= (flags & 16) << 21;
                packet[3] = (uv01 & 0xffff) | (clut << 16);
                packet[6] = (uv01 >> 16) | ((tpage | (flags & 0x60)) << 16);
                packet[9] = uv23;
                if (quad)
                    packet[12] = uv23 >> 16;
                packet += quad ? 13 : 10;
            }
            polygons += quad ? 8 : 6;
            if (kind == 2 && face + 1 < counts[kind])
                flags = polygons[4] >> 24;
        }
    }
    return packet;
}
/* actor_native.c: the native list for the model just built (weak: probes);
 * `kind` 1 for func_800B4594, 3 for func_800B5C30. */
extern void pc_object_native_draw(int kind, const void* projected, const void* object, const void* model,
    const void* color, unsigned* start) __attribute__((weak));
void func_800B4594(void* projected, void* actor, void* unused, void* model)
{
    (void)unused;
    unsigned texture = *(unsigned*)0x1f800390;
    unsigned* start = psx_load(unsigned*, 0x1f800000);
    psx_store(0x1f800000,
        pc_actor_packets(start, psx_load(unsigned*, 0x1f800004), projected,
            (unsigned*)((char*)projected + 0xc00), psx_load(unsigned*, (char*)model + 0x3c),
            (unsigned short*)((char*)actor + 2), texture & 0xffff, texture >> 16));
    if (pc_object_native_draw)
        pc_object_native_draw(1, projected, actor, model, 0, start);
}

/* ---------------------------------------------------------------------------
 * 4B16C.s func_800B5C30: semi-transparent variant of func_800B4594. Records
 * are the same per-vertex-colored layouts, walked in material groups like
 * func_800B516C (8 counts at the model, then the remainder; CLUT + 0x480 for
 * groups flagged in object byte 0x64). When object byte 9 selects a blend
 * mode (1-6 tpage 0x40, 7+ 0x20) every vertex takes the object color and
 * the code becomes GT3/GT4 semi-transparent; mixed records keep their bias
 * byte in the third color. Differences from func_800B516C kept as-is: an
 * empty section does not skip its 8 counts, a depth-rejected triangle still
 * writes part of its packet, and the walk state is left in 0x1F80039C..A3. */
static int actor_nclip(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 12);
    MTC2(b, 13);
    MTC2(c, 14);
    gte_nclip();
    return MFC2(24);
}
static unsigned actor_avsz3(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 17);
    MTC2(b, 18);
    MTC2(c, 19);
    gte_avsz3();
    return MFC2(7);
}
static void actor_link(unsigned* ot, int bucket, unsigned* packet, unsigned words)
{
    unsigned* head = ot + bucket;
    packet[0] = (*head & 0xffffff) | (words << 24);
    *head = (*head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
}
/* Next non-empty material group of a section, or 0 when it is finished. The
 * counter and running total live in scratchpad halfwords 0x39C/0x39E. */
static int actor_blend_group(const unsigned short** counts, unsigned section_total,
    int signed_counts, unsigned mask, unsigned* clut_hi)
{
    unsigned short* state = (unsigned short*)0x1f80039c;
    for (;;) {
        int counter = state[0] - 1;
        state[0] = counter;
        if (counter < 0)
            return 0;
        unsigned clut = *(unsigned short*)0x1f800392;
        int count;
        if (!counter) {
            count = (int)section_total - state[1];
            if (!count)
                return 0;
        } else {
            count = signed_counts ? *(const short*)*counts : **counts;
            ++*counts;
            state[1] += count;
            if (!count)
                continue;
            if ((mask >> (counter - 1)) & 1)
                clut += 0x480;
        }
        if (count < 0)
            pc_unimplemented("negative actor material group count");
        *clut_hi = clut << 16;
        return count;
    }
}
void func_800B5C30(void* projected, void* object, void* color, void* model)
{
    const unsigned char* obj = object;
    const unsigned* xy = projected;
    const unsigned* z = (const unsigned*)((const char*)projected + 0xc00);
    const unsigned short* totals = (const unsigned short*)(obj + 2);
    const unsigned short* counts = model;
    const unsigned* r = psx_load(const unsigned*, (char*)model + 0x3c);
    unsigned* packet = psx_load(unsigned*, 0x1f800000);
    unsigned* start = packet;
    unsigned* ot = psx_load(unsigned*, 0x1f800004);
    unsigned tpage_hi = *(unsigned*)0x1f800390 << 16;
    unsigned tint = *(unsigned*)color & 0xffffff;
    unsigned mode = obj[9] & 0xf;
    unsigned blend = !mode ? 0 : mode < 7 ? 0x400000 : 0x200000;
    unsigned mask = obj[0x64];
    unsigned clut_hi = 0;
    int count;

    /* Triangles, 24-byte records: o0|o1, o2|uv2, c0, c1, c2, uv0|uv1. */
    *(unsigned*)0x1f80039c = 9;
    if (totals[0]) {
        while ((count = actor_blend_group(&counts, totals[0], 1, mask, &clut_hi))) {
            for (; count; --count, r += 6) {
                unsigned i0 = actor_vertex(r[0] & 0xffff), i1 = actor_vertex(r[0] >> 16),
                         i2 = actor_vertex(r[1] & 0xffff);
                if (actor_nclip(xy[i0], xy[i1], xy[i2]) <= 0)
                    continue;
                unsigned c0 = r[2], c1 = r[3], c2 = r[4];
                if (blend)
                    c0 = tint | 0x36000000, c1 = c2 = tint;
                unsigned depth = actor_avsz3(z[i0], z[i1], z[i2]);
                /* Written before the depth test, so they stay on rejection. */
                packet[1] = c0;
                packet[2] = xy[i0];
                packet[3] = (r[5] & 0xffff) | clut_hi;
                packet[4] = c1;
                packet[5] = xy[i1];
                if (depth >= 0x7c0)
                    continue;
                packet[6] = (r[5] >> 16) | tpage_hi | blend;
                packet[7] = c2;
                packet[8] = xy[i2];
                packet[9] = r[1] >> 16;
                actor_link(ot, (int)depth - 1, packet, 9);
                packet += 10;
            }
        }
    }
    /* Quads, 32-byte records: o0|o1, o2|o3, c0..c3, uv0|uv1, uv2|uv3. */
    *(unsigned*)0x1f80039c = 9;
    if (totals[1]) {
        while ((count = actor_blend_group(&counts, totals[1], 1, mask, &clut_hi))) {
            for (; count; --count, r += 8) {
                unsigned i0 = actor_vertex(r[0] & 0xffff), i1 = actor_vertex(r[0] >> 16),
                         i2 = actor_vertex(r[1] & 0xffff), i3 = actor_vertex(r[1] >> 16);
                if (actor_nclip(xy[i0], xy[i1], xy[i2]) <= 0)
                    continue;
                unsigned depth = (z[i0] + z[i1] + z[i2] + z[i3]) >> 4;
                if (depth >= 0x7c0)
                    continue;
                unsigned c0 = r[2], c1 = r[3], c2 = r[4], c3 = r[5];
                if (blend)
                    c0 = tint | 0x3e000000, c1 = c2 = c3 = tint;
                packet[1] = c0;
                packet[2] = xy[i0];
                packet[3] = (r[6] & 0xffff) | clut_hi;
                packet[4] = c1;
                packet[5] = xy[i1];
                packet[6] = (r[6] >> 16) | tpage_hi | blend;
                packet[7] = c2;
                packet[8] = xy[i2];
                packet[9] = r[7];
                packet[10] = c3;
                packet[11] = xy[i3];
                packet[12] = r[7] >> 16;
                actor_link(ot, (int)depth - 1, packet, 12);
                packet += 13;
            }
        }
    }
    /* Mixed records: triangle or quad by the c0 code (0x3c), flags and bias
     * in the c2 top byte, stride in the c1 top byte when culled. */
    *(unsigned*)0x1f80039c = 9;
    psx_store(0x1f8003a0, object);
    if (totals[2]) {
        while ((count = actor_blend_group(&counts, totals[2], 0, mask, &clut_hi))) {
            unsigned flags = r[4] >> 24;
            for (; count; --count) {
                unsigned i0 = actor_vertex(r[0] & 0xffff), i1 = actor_vertex(r[0] >> 16),
                         i2 = actor_vertex(r[1] & 0xffff);
                if (!(flags & 1) && actor_nclip(xy[i0], xy[i1], xy[i2]) <= 0) {
                    unsigned stride = r[3] >> 24;
                    if (stride < 24 || (stride & 3))
                        pc_unimplemented("actor polygon stride");
                    r += stride / 4;
                    /* As in func_800B4594, the culled branch reloads flags
                     * from word 0 of the next record. */
                    if (count > 1)
                        flags = r[0] >> 24;
                    continue;
                }
                unsigned c0 = r[2], c1 = r[3], c2 = r[4];
                if ((c0 >> 24) == 0x3c) {
                    unsigned i3 = actor_vertex(r[1] >> 16);
                    unsigned depth = (z[i0] + z[i1] + z[i2] + z[i3]) >> 4;
                    if (depth < 0x7c0) {
                        unsigned c3 = r[5];
                        if (blend)
                            c0 = tint | 0x3e000000, c1 = c3 = tint,
                            c2 = (c2 & 0xff000000) | tint;
                        packet[1] = c0;
                        packet[2] = xy[i0];
                        packet[3] = (r[6] & 0xffff) | clut_hi;
                        packet[4] = c1;
                        packet[5] = xy[i1];
                        packet[6] = (r[6] >> 16) | tpage_hi | blend;
                        packet[7] = c2;
                        packet[8] = xy[i2];
                        packet[9] = r[7];
                        packet[10] = c3;
                        packet[11] = xy[i3];
                        packet[12] = r[7] >> 16;
                        actor_link(ot, (int)(depth + ((c2 >> 26) & 3) * 4) - 5, packet, 12);
                        packet += 13;
                    }
                    r += 8;
                } else {
                    if (blend)
                        c0 = tint | 0x36000000, c1 = tint, c2 = (c2 & 0xff000000) | tint;
                    unsigned depth = actor_avsz3(z[i0], z[i1], z[i2]);
                    /* Written before the depth test, so they stay on rejection. */
                    packet[1] = c0;
                    packet[2] = xy[i0];
                    if (depth < 0x7c0) {
                        packet[3] = (r[5] & 0xffff) | clut_hi;
                        packet[4] = c1;
                        packet[5] = xy[i1];
                        packet[6] = (r[5] >> 16) | tpage_hi | blend;
                        packet[7] = c2;
                        packet[8] = xy[i2];
                        packet[9] = r[1] >> 16;
                        actor_link(ot, (int)(depth + ((c2 >> 26) & 3) * 4) - 5, packet, 9);
                        packet += 10;
                    }
                    r += 6;
                }
                if (count > 1)
                    flags = r[4] >> 24;
            }
        }
    }
    psx_store(0x1f800000, packet);
    if (pc_object_native_draw)
        pc_object_native_draw(3, projected, object, model, color, start);
}
