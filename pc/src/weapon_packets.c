/* Native packet emission from BATTLE/4B16C.s func_800B3CF0.
 * The projected arrays, packed polygon records and 24-bit OT links stay intact. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void pc_unimplemented(const char*);

static unsigned vertex_index(unsigned offset)
{
    if ((offset & 3) || offset >= 0xc00)
        pc_unimplemented("weapon polygon vertex offset");
    return offset >> 2;
}
static int area(const unsigned* xy)
{
    MTC2(xy[0], 12);
    MTC2(xy[1], 13);
    MTC2(xy[2], 14);
    gte_nclip();
    return MFC2(24);
}
static unsigned average_depth(const unsigned* z, int quad)
{
    if (quad)
        return (z[0] + z[1] + z[2] + z[3]) >> 4;
    MTC2(z[0], 17);
    MTC2(z[1], 18);
    MTC2(z[2], 19);
    gte_avsz3();
    return MFC2(7);
}
static unsigned* emit(unsigned* packet, unsigned* ot, int bucket, unsigned color,
    unsigned tpage, unsigned clut, const unsigned* xy, const unsigned* uv, int quad)
{
    unsigned words = quad ? 9 : 7;
    unsigned* head = ot + bucket;
    packet[0] = (*head & 0xffffff) | (words << 24);
    *head = (*head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
    packet[1] = color;
    for (int i = 0; i < (quad ? 4 : 3); i++) {
        packet[2 + i * 2] = xy[i];
        packet[3 + i * 2] = uv[i];
    }
    packet[3] |= clut << 16;
    packet[5] |= tpage << 16;
    return packet + words + 1;
}
/* Pick the original non-planar quad diagonal using the GTE OP determinant.
 * Arithmetic below is explicitly 32-bit wrapping, like the MIPS multiplies. */
static int split_diagonal(const unsigned* xy, const unsigned* z)
{
    int x0 = (short)xy[0], y0 = (short)(xy[0] >> 16);
    CTC2((short)xy[2] - x0, 0);
    CTC2((short)(xy[2] >> 16) - y0, 2);
    CTC2(z[2] - z[0], 4);
    MTC2((short)xy[1] - x0, 9);
    MTC2((short)(xy[1] >> 16) - y0, 10);
    MTC2(z[1] - z[0], 11);
    gte_op0();
    unsigned dot = ((unsigned)((short)xy[3] - x0) * MFC2(25))
                 + ((unsigned)((short)(xy[3] >> 16) - y0) * MFC2(26))
                 + ((z[3] - z[0]) * MFC2(27));
    return (int)(0u - dot) > 0;
}
static unsigned* split_quad(unsigned* packet, unsigned* ot, unsigned color,
    unsigned tpage, unsigned clut, const unsigned* xy, const unsigned* z,
    const unsigned* uv)
{
    static const unsigned indices[2][2][3] = { { { 0, 1, 3 }, { 0, 3, 2 } },
        { { 0, 1, 2 }, { 1, 3, 2 } } };
    int diagonal = split_diagonal(xy, z);
    for (unsigned face = 0; face < 2; ++face) {
        unsigned tx[3], tz[3], tu[3];
        for (unsigned i = 0; i < 3; ++i) {
            unsigned n = indices[diagonal][face][i];
            tx[i] = xy[n];
            tz[i] = z[n];
            tu[i] = uv[n];
        }
        if (area(tx) <= 0) {
            /* The original reserves an unlinked slot in this one branch. */
            if (diagonal == 0 && face == 1)
                packet += 8;
            continue;
        }
        unsigned depth = average_depth(tz, 0) + 4;
        if (depth < 0x7c0)
            packet = emit(packet, ot, (int)depth - 5, (color & 0xffffff) | 0x24000000,
                tpage, clut, tx, tu, 0);
    }
    return packet;
}
unsigned* pc_weapon_packets(unsigned* packet, unsigned* ot, const unsigned* projected,
    const unsigned* depths, const unsigned* polygons, const unsigned short counts[3],
    unsigned color, unsigned tpage, unsigned clut)
{
    for (unsigned kind = 0; kind < 3; ++kind) {
        for (unsigned face = 0; face < counts[kind]; ++face) {
            unsigned header = polygons[0];
            int quad = kind == 1 || (kind == 2 && (header & 255) == 0x2c);
            unsigned flags = kind == 2 ? header >> 16 : 0;
            unsigned xy[4], z[4], uv[4];
            unsigned offsets[4] = { polygons[1] & 0xffff, polygons[1] >> 16,
                polygons[2] & 0xffff, polygons[2] >> 16 };
            for (int i = 0; i < (quad ? 4 : 3); i++) {
                unsigned index = vertex_index(offsets[i]);
                xy[i] = projected[index];
                z[i] = depths[index];
            }
            unsigned stride = quad ? 5 : 4;
            if (!(flags & 1) && area(xy) <= 0) {
                if (kind == 2) {
                    unsigned bytes = (header >> 8) & 255;
                    if (bytes < 16 || (bytes & 3))
                        pc_unimplemented("weapon polygon stride");
                    stride = bytes / 4;
                }
                polygons += stride;
                continue;
            }

            uv[0] = polygons[3] & 0xffff;
            uv[1] = polygons[3] >> 16;
            uv[2] = quad ? polygons[4] : polygons[2] >> 16;
            uv[3] = quad ? polygons[4] >> 16 : 0;
            if ((flags & 3) == 3) {
                if (!quad)
                    pc_unimplemented("split flag on non-quad weapon face");
                packet = split_quad(packet, ot, color, tpage, clut, xy, z, uv);
                polygons += 5;
                continue;
            }
            unsigned depth =
                average_depth(z, quad) + (kind == 2 ? ((flags >> 2) & 3) * 4 : 0);
            if (depth < 0x7c0) {
                unsigned code = quad ? 0x2c : 0x24;
                if (flags & 16)
                    code |= 2;
                packet = emit(packet, ot, (int)depth - (kind == 2 ? 5 : 1),
                    (color & 0xffffff) | (code << 24), tpage | (flags & 0x60), clut, xy,
                    uv, quad);
            }
            polygons += stride;
        }
    }
    return packet;
}
/* actor_native.c: the native list for the model just built (weak: probes);
 * `kind` 0 for func_800B3CF0, 2 for func_800B516C. */
extern void pc_object_native_draw(int kind, const void* projected, const void* object, const void* model,
    const void* color, unsigned* start) __attribute__((weak));
void func_800B3CF0(void* projected, void* object, void* color, void* model)
{
    unsigned texture = *(unsigned*)0x1f800390;
    unsigned* start = psx_load(unsigned*, 0x1f800000);
    psx_store(0x1f800000, pc_weapon_packets(start,
        psx_load(unsigned*, 0x1f800004), projected, (unsigned*)((char*)projected + 0xc00),
        psx_load(unsigned*, (char*)model + 0x3c), (unsigned short*)((char*)object + 2),
        *(unsigned*)color, texture & 0xffff, texture >> 16));
    if (pc_object_native_draw)
        pc_object_native_draw(0, projected, object, model, color, start);
}

/* ---------------------------------------------------------------------------
 * 4B16C.s func_800B516C: the weapon renderer used while the actor is in a
 * special state (e.g. paused). Unlike func_800B3CF0 it walks each polygon
 * section in material groups (8 counts at the model, then the remainder),
 * swaps in the alternate CLUT (+0x480) for groups flagged in object byte 0x64,
 * and makes polygons semi-transparent when object byte 9 selects a blend
 * mode (1-6 subtractive tpage 0x40, 7+ additive 0x20). OT buckets: triangles
 * and quads depth - 1, mixed records depth - 5 plus their bias; split quads
 * depth - 1. Packets are written in the original order, including the slot
 * the split path reserves without linking. */
struct blend_state {
    unsigned* packet;
    unsigned* ot;
    unsigned color, tpage_hi, blend, clut_hi;
};
static void link_packet(struct blend_state* s, unsigned* packet, int bucket, unsigned words)
{
    unsigned* head = s->ot + bucket;
    packet[0] = (*head & 0xffffff) | (words << 24);
    *head = (*head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
}
/* Group walk shared by the three sections: returns the CLUT word (<< 16) and
 * count of the next non-empty group, or 0 when the section is finished. */
struct group_walk {
    int counter, total, done;
    const unsigned short* counts;
};
static int next_group(struct group_walk* g, unsigned section_total, unsigned mask,
    int clut, int alternate_signed, unsigned* clut_hi)
{
    for (;;) {
        if (--g->counter < 0)
            return 0;
        int count;
        if (g->counter == 0) {
            count = (int)section_total - g->total;
            if (!count)
                return 0;
            *clut_hi = (unsigned)clut << 16;
            return count;
        }
        count = alternate_signed ? *(const short*)g->counts : *g->counts;
        ++g->counts;
        g->total += count;
        if (!count)
            continue;
        int value = clut + (((mask >> (g->counter - 1)) & 1) ? 0x480 : 0);
        *clut_hi = (unsigned)value << 16;
        return count;
    }
}
static int nclip3(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 12);
    MTC2(b, 13);
    MTC2(c, 14);
    gte_nclip();
    return MFC2(24);
}
static unsigned avsz3(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 17);
    MTC2(b, 18);
    MTC2(c, 19);
    gte_avsz3();
    return MFC2(7);
}
static void blend_triangle(struct blend_state* s, unsigned code, const unsigned xy[3],
    unsigned uv0, unsigned uv1, unsigned uv2, unsigned depth, int bucket_bias,
    unsigned tpage_bits)
{
    unsigned* p = s->packet;
    p[1] = code | s->color;
    p[2] = xy[0];
    p[3] = (uv0 & 0xffff) | s->clut_hi;
    if (depth >= 0x7c0)
        return;
    p[4] = xy[1];
    p[5] = (uv1 & 0xffff) | s->tpage_hi | tpage_bits;
    p[6] = xy[2];
    p[7] = uv2;
    link_packet(s, p, (int)depth + bucket_bias, 7);
    s->packet = p + 8;
}
static void blend_quad(struct blend_state* s, unsigned code, const unsigned xy[4],
    unsigned uv01, unsigned uv23, unsigned depth, int bucket_bias, unsigned tpage_bits)
{
    if (depth >= 0x7c0)
        return;
    unsigned* p = s->packet;
    p[1] = code | s->color;
    p[2] = xy[0];
    p[3] = (uv01 & 0xffff) | s->clut_hi;
    p[4] = xy[1];
    p[5] = (uv01 >> 16) | s->tpage_hi | tpage_bits;
    p[6] = xy[2];
    p[7] = uv23;
    p[8] = xy[3];
    p[9] = uv23 >> 16;
    link_packet(s, p, (int)depth + bucket_bias, 9);
    s->packet = p + 10;
}
/* Double-sided split quad (mixed records with flags 1 and 2). */
static void blend_split(struct blend_state* s, const unsigned xy[4], const unsigned z[4],
    unsigned uv01, unsigned uv23)
{
    unsigned code = s->blend ? 0x26000000 : 0x24000000;
    unsigned tri[3];
    if (!split_diagonal(xy, z)) {
        tri[0] = xy[0], tri[1] = xy[1], tri[2] = xy[3];
        if (nclip3(tri[0], tri[1], tri[2]) > 0)
            blend_triangle(s, code, tri, uv01, uv01 >> 16, uv23 >> 16,
                avsz3(z[0], z[1], z[3]), -1, s->blend);
        tri[0] = xy[0], tri[1] = xy[3], tri[2] = xy[2];
        unsigned* p = s->packet;
        p[1] = code | s->color;
        p[2] = xy[0];
        if (nclip3(tri[0], tri[1], tri[2]) <= 0) {
            s->packet = p + 8; /* reserved without a link, as in the original */
            return;
        }
        blend_triangle(s, code, tri, uv01, uv23 >> 16, uv23, avsz3(z[0], z[2], z[3]), -1,
            s->blend);
        return;
    }
    tri[0] = xy[0], tri[1] = xy[1], tri[2] = xy[2];
    if (nclip3(tri[0], tri[1], tri[2]) > 0)
        blend_triangle(s, code, tri, uv01, uv01 >> 16, uv23, avsz3(z[0], z[1], z[2]), -1,
            s->blend);
    tri[0] = xy[1], tri[1] = xy[3], tri[2] = xy[2];
    if (nclip3(tri[0], tri[1], tri[2]) > 0)
        blend_triangle(s, code, tri, uv01 >> 16, uv23 >> 16, uv23,
            avsz3(z[3], z[1], z[2]), -1, s->blend);
}
void func_800B516C(void* projected, void* object, void* color, void* model)
{
    unsigned char* obj = object;
    const unsigned char* xy_base = projected;
    const unsigned char* z_base = xy_base + 0xc00;
    unsigned texture = *(unsigned*)0x1f800390;
    int clut = *(short*)0x1f800392;
    struct blend_state s = { psx_load(unsigned*, 0x1f800000), psx_load(unsigned*, 0x1f800004),
        *(unsigned*)color & 0xffffff, texture << 16, 0, 0 };
    unsigned* start = s.packet;
    unsigned mode = obj[9] & 0xf;
    s.blend = !mode ? 0 : mode < 7 ? 0x400000 : 0x200000;
    unsigned mask = obj[0x64];
    const unsigned char* record = psx_load(unsigned char*, (unsigned char*)model + 0x3c);
    const unsigned short* counts = model;
    unsigned short* totals = (unsigned short*)(obj + 2);
#define XY(offset) (*(const unsigned*)(xy_base + (offset)))
#define Z(offset) (*(const unsigned*)(z_base + (offset)))
    /* Triangles (16-byte records) and quads (20-byte records). */
    for (int section = 0; section < 2; ++section) {
        int quad = section == 1;
        struct group_walk g = { 9, 0, 0, counts };
        if (totals[section]) {
            int count;
            while ((count = next_group(&g, totals[section], mask, clut, 1, &s.clut_hi))) {
                for (; count; --count, record += quad ? 0x14 : 0x10) {
                    const unsigned* r = (const unsigned*)record;
                    unsigned o0 = r[1] & 0xffff, o1 = r[1] >> 16, o2 = r[2] & 0xffff;
                    unsigned xy[4] = { XY(o0), XY(o1), XY(o2), 0 };
                    if (nclip3(xy[0], xy[1], xy[2]) <= 0)
                        continue;
                    if (!quad) {
                        blend_triangle(&s, s.blend ? 0x26000000 : 0x24000000, xy, r[3],
                            r[3] >> 16, r[2] >> 16, avsz3(Z(o0), Z(o1), Z(o2)), -1, s.blend);
                        continue;
                    }
                    unsigned o3 = r[2] >> 16;
                    xy[3] = XY(o3);
                    unsigned depth = (Z(o0) + Z(o1) + Z(o2) + Z(o3)) >> 4;
                    blend_quad(&s, s.blend ? 0x2e000000 : 0x2c000000, xy, r[3], r[4], depth,
                        -1, s.blend);
                }
            }
            counts = g.counts;
        } else {
            counts += 8;
        }
    }
    /* Mixed records: header (code, size, flags << 16), then as above. */
    struct group_walk g = { 9, 0, 0, counts };
    int count;
    while (totals[2]
        && (count = next_group(&g, totals[2], obj[0x64], (unsigned short)clut, 0, &s.clut_hi))) {
        for (; count; --count) {
            const unsigned* r = (const unsigned*)record;
            unsigned header = r[0], flags = header >> 16;
            unsigned o0 = r[1] & 0xffff, o1 = r[1] >> 16, o2 = r[2] & 0xffff, o3 = r[2] >> 16;
            unsigned xy[4] = { XY(o0), XY(o1), XY(o2), XY(o3) };
            if (!(flags & 1)) {
                if (nclip3(xy[0], xy[1], xy[2]) <= 0) {
                    record += (header >> 8) & 0xff;
                    continue;
                }
            } else if (flags & 2) {
                unsigned z[4] = { Z(o0), Z(o1), Z(o2), Z(o3) };
                blend_split(&s, xy, z, r[3], r[4]);
                record += 0x14;
                continue;
            }
            if (s.blend)
                header = (header & 0xff0fffff) | 0x100000 | s.blend;
            unsigned code_bit = ((header >> 20) & 1) << 25;
            unsigned tpage_bits = ((header >> 16) & 0x60) << 16;
            int bias = (int)((header >> 18) & 3) * 4 - 5;
            if ((header & 0xff) == 0x2c) {
                unsigned depth = (Z(o0) + Z(o1) + Z(o2) + Z(o3)) >> 4;
                blend_quad(&s, 0x2c000000 | code_bit, xy, r[3], r[4], depth, bias, tpage_bits);
                record += 0x14;
            } else {
                blend_triangle(&s, 0x24000000 | code_bit, xy, r[3], r[3] >> 16, o3,
                    avsz3(Z(o0), Z(o1), Z(o2)), bias, tpage_bits);
                record += 0x10;
            }
        }
    }
#undef XY
#undef Z
    psx_store(0x1f800000, s.packet);
    if (pc_object_native_draw)
        pc_object_native_draw(2, projected, object, model, color, start);
}
