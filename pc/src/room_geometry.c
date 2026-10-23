/* Native room packet submission from BATTLE/2EA3C.s. The original packed
 * geometry and ordering-table layout are retained, including the adaptive
 * midpoint subdivision of near, large polygons (func_80097AEC/CF0/F70). */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void pc_unimplemented(const char*);
extern int pc_room_quad_visible(const unsigned[4]);
extern int pc_room_depth_bucket(unsigned, int);
extern int pc_room_within_distance(unsigned);
/* The renderer's record of each room packet's vertex depths (gpu_hw.c), so
 * room polygons need not be matched to projections by screen position. */
extern void pc_hw_room_depths(const void* packet, const unsigned* xy, const unsigned* sz, int count) __attribute__((weak));

static void decode_vertices(
    const unsigned* polygon, unsigned shift, SVECTOR v[4], int quad)
{
    int x = (short)polygon[0], y = (short)(polygon[0] >> 16), z = (short)polygon[1];
    const signed char* delta = (const signed char*)polygon + 6;
    v[0] = (SVECTOR) { x, y, z, 0 };
    for (int i = 1; i < 3; ++i, delta += 3) {
        v[i] = (SVECTOR) { x + ((unsigned)(int)delta[0] << shift),
            y + ((unsigned)(int)delta[1] << shift),
            z + ((unsigned)(int)delta[2] << shift), 0 };
    }
    if (quad) {
        delta = (const signed char*)(polygon + 8);
        v[3] = (SVECTOR) { x + ((unsigned)(int)delta[0] << shift),
            y + ((unsigned)(int)delta[1] << shift),
            z + ((unsigned)(int)delta[2] << shift), 0 };
    }
}
static unsigned pack_xy(SVECTOR* v)
{
    return (unsigned short)v->vx | ((unsigned)(unsigned short)v->vy << 16);
}
static int projected_area(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 12);
    MTC2(b, 13);
    MTC2(c, 14);
    gte_nclip();
    return MFC2(24);
}
/* Subdivision vertex record, laid out as the original 0x14-byte scratchpad
 * records: model XYZ, UV, colour word, projected XY and depth. */
typedef struct {
    short x, y, z;
    unsigned char u, v;
    unsigned color;
    unsigned sxy;
    unsigned sz;
} RoomVertex;
typedef struct {
    unsigned* packet;
    unsigned* ot;
    unsigned* limit;
    int bias;
    unsigned code; /* 0x1F8003B8: polygon code bits */
    unsigned tpage; /* 0x1F8003BC */
    unsigned clut; /* 0x1F8003C0 */
} RoomSubdivision;

/* func_80097F70: byte colour/UV averages, arithmetic-shift XYZ midpoints.
 * The colour word's top byte is not written by the original. */
static void midpoint(const RoomVertex* a, const RoomVertex* b, RoomVertex* m)
{
    unsigned char* mc = (unsigned char*)&m->color;
    const unsigned char *ac = (const unsigned char*)&a->color,
                        *bc = (const unsigned char*)&b->color;
    m->color = 0; /* original leaves the scratchpad byte unchanged */
    for (int i = 0; i < 3; ++i)
        mc[i] = (ac[i] + bc[i]) >> 1;
    m->x = (a->x + b->x) >> 1;
    m->y = (a->y + b->y) >> 1;
    m->z = (a->z + b->z) >> 1;
    m->u = (a->u + b->u) >> 1;
    m->v = (a->v + b->v) >> 1;
}
static void load_vertex(const RoomVertex* v, int slot)
{
    MTC2((unsigned short)v->x | ((unsigned)(unsigned short)v->y << 16), slot * 2);
    MTC2(v->z, slot * 2 + 1);
}
static void project3(RoomVertex* a, RoomVertex* b, RoomVertex* c)
{
    load_vertex(a, 0);
    load_vertex(b, 1);
    load_vertex(c, 2);
    gte_rtpt();
    a->sxy = MFC2(12);
    b->sxy = MFC2(13);
    c->sxy = MFC2(14);
    a->sz = MFC2(17);
    b->sz = MFC2(18);
    c->sz = MFC2(19);
}
/* Leaf submission shared by func_80097AEC/CF0 at depth zero. Leaves skip the
 * backface and distance-UV tests of the top level, as in the original. */
static void submit_leaf(RoomSubdivision* s, RoomVertex* const* v, int quad)
{
    int count = quad ? 4 : 3, words = quad ? 13 : 10;
    unsigned xy[4];
    for (int i = 0; i < 4; ++i)
        xy[i] = v[i < count ? i : 2]->sxy;
    if (!pc_room_quad_visible(xy))
        return;
    int depth = (short)v[0]->sz;
    for (int i = 1; i < count; ++i)
        if ((short)v[i]->sz > depth)
            depth = (short)v[i]->sz;
    int bucket = pc_room_depth_bucket((unsigned)depth, s->bias);
    if (bucket < 0)
        return;
    unsigned* packet = s->packet;
    if (packet + words > s->limit)
        pc_unimplemented("room packet capacity");
    packet[0] = (s->ot[bucket] & 0xffffff) | ((unsigned)(words - 1) << 24);
    s->ot[bucket] = (s->ot[bucket] & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
    for (int i = 0; i < count; ++i) {
        packet[1 + i * 3] = v[i]->color;
        packet[2 + i * 3] = v[i]->sxy;
        packet[3 + i * 3] = v[i]->u | ((unsigned)v[i]->v << 8);
    }
    if (pc_hw_room_depths) {
        unsigned sz[4], sxy[4];
        for (int i = 0; i < count; ++i)
            sz[i] = v[i]->sz, sxy[i] = v[i]->sxy;
        pc_hw_room_depths(packet, sxy, sz, count);
    }
    packet[1] = (v[0]->color & 0xffffff) | (quad ? s->code : s->code & 0xf7000000);
    packet[3] |= s->clut;
    packet[6] |= s->tpage;
    s->packet = packet + words;
}
/* func_80097AEC: four recursive triangles, then three depth-zero edge
 * triangles (degenerate in model space) in the original submission order. */
static void subdivide_triangle(
    RoomSubdivision* s, int level, RoomVertex* a, RoomVertex* b, RoomVertex* c)
{
    if (level == 0) {
        RoomVertex* v[3] = { a, b, c };
        submit_leaf(s, v, 0);
        return;
    }
    RoomVertex ab, cb, ca;
    midpoint(a, b, &ab);
    midpoint(c, b, &cb);
    midpoint(c, a, &ca);
    project3(&ab, &cb, &ca);
    --level;
    subdivide_triangle(s, level, a, &ab, &ca);
    subdivide_triangle(s, level, &cb, &ab, b);
    subdivide_triangle(s, level, &cb, c, &ca);
    subdivide_triangle(s, level, &ab, &cb, &ca);
    subdivide_triangle(s, 0, c, a, &ca);
    subdivide_triangle(s, 0, c, &cb, b);
    subdivide_triangle(s, 0, &ab, a, b);
}
/* func_80097CF0: four recursive quads, then four depth-zero triangles. */
static void subdivide_quad(RoomSubdivision* s, int level, RoomVertex* a,
    RoomVertex* b, RoomVertex* c, RoomVertex* d)
{
    if (level == 0) {
        RoomVertex* v[4] = { a, b, c, d };
        submit_leaf(s, v, 1);
        return;
    }
    RoomVertex ab, cb, db, dc, ac;
    midpoint(a, b, &ab);
    midpoint(c, b, &cb);
    midpoint(d, b, &db);
    project3(&ab, &cb, &db);
    midpoint(d, c, &dc);
    midpoint(a, c, &ac);
    project3(&dc, &ac, &db);
    --level;
    subdivide_quad(s, level, a, &ab, &ac, &cb);
    subdivide_quad(s, level, b, &ab, &db, &cb);
    subdivide_quad(s, level, d, &dc, &db, &cb);
    subdivide_quad(s, level, c, &dc, &ac, &cb);
    subdivide_triangle(s, 0, c, &dc, d);
    subdivide_triangle(s, 0, c, a, &ac);
    subdivide_triangle(s, 0, &ab, a, b);
    subdivide_triangle(s, 0, d, &db, b);
}
/* func_80097388's setup for the subdivision path: build one record per
 * polygon vertex from the projected values and the packed polygon words. */
static unsigned* subdivide_polygon(const unsigned* poly, const SVECTOR* vertex,
    const unsigned* xy, const unsigned* sz, int quad, int level, unsigned* packet,
    unsigned* ot, unsigned gp, unsigned* limit)
{
    RoomSubdivision s = { packet, ot, limit, (int)gp >> 16, poly[3] & 0xfe000000,
        poly[7] & 0xffff0000, poly[6] & 0xffff0000 };
    RoomVertex v[4];
    unsigned uv[4] = { poly[6], poly[7], (poly[4] >> 24) | ((poly[5] >> 24) << 8),
        quad ? (poly[8] >> 24) | ((poly[9] >> 24) << 8) : 0 };
    unsigned colors[4] = { poly[3], poly[4], poly[5], quad ? poly[9] : 0 };
    for (int i = 0; i < (quad ? 4 : 3); ++i) {
        v[i].x = vertex[i].vx;
        v[i].y = vertex[i].vy;
        v[i].z = vertex[i].vz;
        v[i].u = uv[i];
        v[i].v = uv[i] >> 8;
        v[i].color = (colors[i] & 0xffffff) | 0x1000000;
        v[i].sxy = xy[i];
        v[i].sz = sz[i];
    }
    if (quad)
        subdivide_quad(&s, level, &v[0], &v[1], &v[2], &v[3]);
    else
        subdivide_triangle(&s, level, &v[0], &v[1], &v[2]);
    return s.packet;
}
/* With the native room only (pc_native_room 2): emit just the
 * semi-transparent polygons; the native list draws the opaque ones. */
static int translucent_only;
/* room_native.c: a translucent marker in front of each semi-transparent
 * polygon's packet (the native list draws it there). */
extern unsigned* pc_room_native_translucent(unsigned index, unsigned* packet, unsigned* ot, int bucket, int halve,
    const unsigned* limit) __attribute__((weak));
extern void pc_room_render_rules(int* near_bucket, int* far_bucket, int* lod_bucket);
static int native;
unsigned* pc_room_group_packets(unsigned* geometry, unsigned* packet, unsigned* ot,
    unsigned gp, unsigned mode, unsigned* limit)
{
    unsigned triangles = geometry[0], quads = geometry[1];
    unsigned* poly = geometry + 2;
    if (triangles > 65535 || quads > 65535)
        pc_unimplemented("room polygon count");
    for (unsigned i = 0; i < triangles + quads; ++i) {
        int quad = i >= triangles, count = quad ? 4 : 3;
        if (translucent_only && !(poly[3] & 0x02000000))
            goto next;
        SVECTOR vertex[4];
        unsigned xy[4], sz[4];
        decode_vertices(poly, gp & 31, vertex, quad);
        MTC2(pack_xy(&vertex[0]), 0);
        MTC2(vertex[0].vz, 1);
        MTC2(pack_xy(&vertex[1]), 2);
        MTC2(vertex[1].vz, 3);
        MTC2(pack_xy(&vertex[2]), 4);
        MTC2(vertex[2].vz, 5);
        gte_rtpt();
        xy[0] = MFC2(12);
        xy[1] = MFC2(13);
        xy[2] = xy[3] = MFC2(14);
        sz[0] = MFC2(17);
        sz[1] = MFC2(18);
        sz[2] = MFC2(19);
        unsigned depth = sz[0], z = sz[1];
        if (z > depth)
            depth = z;
        z = MFC2(19);
        if (z > depth)
            depth = z;
        int area = projected_area(xy[0], xy[1], xy[2]);
        if (!quad && area < 0 && !(poly[3] & 0x01000000))
            goto next;
        if (!quad && area < 0)
            area = -area;
        if (quad) {
            MTC2(pack_xy(&vertex[3]), 0);
            MTC2(vertex[3].vz, 1);
            gte_rtps();
            xy[3] = MFC2(14);
            z = sz[3] = MFC2(19);
            if (z > depth)
                depth = z;
            if (area < 0 && !(poly[3] & 0x01000000)) {
                area = projected_area(xy[3], xy[2], xy[1]);
                if (area < 0)
                    goto next;
            }
        }
        if (!pc_room_quad_visible(xy))
            goto next;
        if ((mode & 1) && (unsigned)area >= 0x200
            && (int)((gp >> 13) & 3) - (int)(depth >> 9) > 0) {
            packet = subdivide_polygon(poly, vertex, xy, sz, quad,
                (int)((gp >> 13) & 3) - (int)(depth >> 9), packet, ot, gp, limit);
            if (native && (poly[3] & 0x02000000) && pc_room_native_translucent) {
                /* One marker for the whole polygon, where its farthest
                 * leaves can be (the leaves' packets are not drawn). */
                int near_bucket, far_bucket, lod_bucket;
                pc_room_render_rules(&near_bucket, &far_bucket, &lod_bucket);
                if ((int)(depth >> 2) >= near_bucket)
                    packet = pc_room_native_translucent(i, packet, ot, (int)(depth >> 2) + ((int)gp >> 16), 0, limit);
            }
            goto next;
        }
        int bucket = pc_room_depth_bucket(depth, (int)gp >> 16);
        if (bucket < 0)
            goto next;
        if (packet + (quad ? 13 : 10) > limit)
            pc_unimplemented("room packet capacity");
        unsigned uv[4] = { poly[6], poly[7], (poly[4] >> 24) | ((poly[5] >> 24) << 8),
            0 };
        unsigned colors[4] = { poly[3] & 0xfeffffff, poly[4], poly[5], 0 };
        if (quad) {
            uv[3] = (poly[8] >> 24) | ((poly[9] >> 24) << 8);
            colors[3] = poly[9];
        }
        if (!pc_room_within_distance(bucket)) {
            uv[0] = (uv[0] & 0xffff0000) | ((uv[0] >> 1) & 0x7f7f);
            uv[1] = ((uv[1] & 0xffff0000) - 0x10000) | ((uv[1] >> 1) & 0x7f7f);
            uv[2] = (uv[2] >> 1) & 0x7f7f;
            uv[3] = (uv[3] >> 1) & 0x7f7f;
        }
        packet[0] = (ot[bucket] & 0xffffff) | ((quad ? 12u : 9u) << 24);
        ot[bucket] = (ot[bucket] & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
        for (int j = 0; j < count; ++j) {
            packet[1 + j * 3] = colors[j];
            packet[2 + j * 3] = xy[j];
            packet[3 + j * 3] = uv[j];
        }
        if (pc_hw_room_depths)
            pc_hw_room_depths(packet, xy, sz, count);
        packet += quad ? 13 : 10;
        if (native && (poly[3] & 0x02000000) && pc_room_native_translucent)
            packet = pc_room_native_translucent(i, packet, ot, bucket, !pc_room_within_distance(bucket), limit);
    next:
        poly += quad ? 10 : 8;
    }
    return packet;
}
extern void pc_hw_room_packets(const void* start, const void* end) __attribute__((weak));
extern int pc_live_camera_active(void) __attribute__((weak));
extern unsigned pc_room_editor_facing(void) __attribute__((weak));
extern int pc_room_editor_hidden(const void* data, unsigned index) __attribute__((weak));
/* Native room geometry (room_native.c, render.h). */
extern int pc_room_native_begin(const void* data, unsigned mode, const unsigned* first) __attribute__((weak));
extern void pc_room_native_group(const void* group, unsigned gp) __attribute__((weak));
extern unsigned* pc_room_native_end(unsigned* packet, unsigned* ot) __attribute__((weak));
extern void pc_room_native_group_cut(float cut, float cut_previous, int ghost) __attribute__((weak));
/* Smart cutaway walls (gameplay_walls.c, gameplay_walls 1). */
extern int pc_gameplay_walls_frame(const void* data, unsigned facing, unsigned mode) __attribute__((weak));
extern int pc_gameplay_walls_group(unsigned index, int kept, float* cut, float* cut_previous, int* ghost)
    __attribute__((weak));
extern void pc_occlusion_frame(int free_camera) __attribute__((weak));
/* room_native.c (room_editor.h): a plugin's free camera sees the whole room. */
extern int pc_room_photo_whole(void) __attribute__((weak));
extern void pc_room_native_tint(const unsigned* start, int dr, int dg, int db, int fade) __attribute__((weak));
extern const void* pc_scene_room_data(void) __attribute__((weak));
/* The current room's groups drawn by the latest call (RPC room_groups
 * "drawn", facing_cutaway_smoke.py): the same in every pc_native_room mode,
 * since this function decides for both paths. */
#define DRAWN_MAX 4096
static const void* drawn_data;
static unsigned drawn_count, drawn_facing;
static unsigned char drawn_groups[DRAWN_MAX];
int pc_room_drawn_groups(unsigned char* out, int max, unsigned* facing)
{
    const void* room = pc_scene_room_data ? pc_scene_room_data() : 0;
    if (!room || room != drawn_data)
        return 0;
    int n = (int)drawn_count < max ? (int)drawn_count : max;
    for (int i = 0; i < n; i++)
        out[i] = drawn_groups[i];
    *facing = drawn_facing;
    return n;
}
void func_8009723C(void* data, unsigned mode)
{
    unsigned* scratch = (unsigned*)0x1f800000;
    unsigned *packet = psx_to_ptr(unsigned*, scratch[0]), *limit = packet + 0x4000;
    unsigned* first = packet;
    unsigned* ot = psx_to_ptr(unsigned*, scratch[1]);
    unsigned count = *(unsigned*)data;
    unsigned char* group = (unsigned char*)data + 4;
    if (count > 4096)
        pc_unimplemented("room group count");
    native = pc_room_native_begin ? pc_room_native_begin(data, mode, first) : 0;
    translucent_only = native == 2;
    int free_camera = pc_live_camera_active && pc_live_camera_active();
    unsigned free_facing = free_camera && pc_room_editor_facing ? pc_room_editor_facing() : 0x10000;
    /* Group flags that leave a group out: 0x100 the room's own (event scripts hide and show groups,
     * func_800919D8 / func_80091998), 0x1000 the hide list for where Ashley stands and the game camera's
     * direction and zoom (room section 11, func_8008C2C0; cleared by func_8008C40C). A plugin's free
     * camera is not the game's: it keeps the groups that list hides for the game camera. */
    unsigned hidden_flags = free_camera && pc_room_photo_whole && pc_room_photo_whole() ? 0x100 : 0x1100;
    int record = pc_scene_room_data && data == pc_scene_room_data() && count <= DRAWN_MAX;
    if (record) {
        drawn_data = data;
        drawn_count = count;
        drawn_facing = free_camera ? free_facing : scratch[4];
        for (unsigned i = 0; i < count; ++i)
            drawn_groups[i] = 0;
    }
    /* gameplay_walls smart: the game camera's view of the current room
     * decides again for the groups the facing mask leaves out. */
    int smart = record && pc_gameplay_walls_frame
        && pc_gameplay_walls_frame(free_camera ? (void*)0 : data, scratch[4], mode);
    for (unsigned i = 0; i < count; ++i, group += 0x40) {
        unsigned flags = *(unsigned*)group, facing = scratch[4];
        /* The facing mask is the game camera's; for the editor's free camera
         * room_native.c derives it from the free camera's yaw (or keeps
         * every group) and leaves out the black shells and the groups the
         * editor hid. */
        if (free_camera)
            facing = free_facing;
        if (packet >= limit)
            break;
        if (flags & hidden_flags || ((flags >> 15) & mode & 1))
            continue;
        if (free_camera && pc_room_editor_hidden && pc_room_editor_hidden(data, i))
            continue;
        int kept = (facing >> 16) || !((flags ^ 255) & facing & 255);
        float cut = 0, cut_previous = 0;
        int ghost = 0;
        if (smart) {
            if (!pc_gameplay_walls_group(i, kept, &cut, &cut_previous, &ghost))
                continue;
        } else if (!kept)
            continue;
        unsigned gp = (flags & 0xffff6000) | (flags & 0x800 ? 0 : 3);
        MATRIX* matrix = (MATRIX*)(group + 0x20);
        SetRotMatrix(matrix);
        SetTransMatrix(matrix);
        unsigned* geometry =
            (unsigned*)((unsigned char*)data + *(unsigned*)(group + 0x1c));
        if (record)
            drawn_groups[i] = 1;
        if (native) {
            if (pc_room_native_group_cut && (cut > 0 || cut_previous > 0))
                pc_room_native_group_cut(cut, cut_previous, ghost);
            pc_room_native_group(group, gp);
        }
        packet = pc_room_group_packets(geometry, packet, ot, gp, mode, limit);
    }
    translucent_only = 0;
    /* Occlusion in play (occlusion.c): who matters and whether the groups
     * just drawn hide them, once per game frame for the current room. */
    if (record && pc_occlusion_frame)
        pc_occlusion_frame(free_camera);
    if (native)
        packet = pc_room_native_end(packet, ot);
    native = 0;
    scratch[0] = psx_to_int(unsigned, packet);
    /* Lighting: these packets are static room geometry (already captured in
     * world space), so the renderer leaves them out of per-frame casters. */
    if (pc_hw_room_packets)
        pc_hw_room_packets(first, packet);
}

/* 2EA3C.s func_80098E50: fade one packet colour word toward its own
 * luminance (r*5 + g*9 + b*2, as the far colour) by `fade` with GTE DPCS,
 * then add the signed per-channel offsets and clamp to 0..255. */
static unsigned tint_color(unsigned color, int fade, int dr, int dg, int db)
{
    int r = color & 0xff, g = (color >> 8) & 0xff, b = (color >> 16) & 0xff;
    if (fade) {
        unsigned luminance = r * 5 + g * 9 + b * 2;
        CTC2(luminance, 21);
        CTC2(luminance, 22);
        CTC2(luminance, 23);
        MTC2(color, 6);
        MTC2(fade, 8);
        gte_dpcs();
        color = MFC2(22);
        r = color & 0xff;
        g = (color >> 8) & 0xff;
        b = (color >> 16) & 0xff;
    }
    r += dr;
    g += dg;
    b += db;
    r = r < 0 ? 0 : r > 0xff ? 0xff : r;
    g = g < 0 ? 0 : g > 0xff ? 0xff : g;
    b = b < 0 ? 0 : b > 0xff ? 0xff : b;
    return (color & 0xff000000) | r | (g << 8) | (b << 16);
}
/* 2EA3C.s: tint the room packets just submitted (from `start` to `end`):
 * 9-word triangles and 12-word quads (plus 8 bytes each when flags bit 1 is
 * set); any other packet ends the pass. Offsets are signed bytes * 4. */
extern int render_translucent_marker(const unsigned*, unsigned*, int*, int*) __attribute__((weak));
void func_80098D6C(unsigned* start, unsigned* end, signed char* params, int flags)
{
    int dr = params[0] * 4, dg = params[1] * 4, db = params[2] * 4;
    int fade = *(short*)(params + 4), extra = (flags & 2) * 4;
    if (pc_room_native_tint)
        pc_room_native_tint(start, dr, dg, db, fade);
    unsigned char* packet = (unsigned char*)start;
    while (packet != (unsigned char*)end) {
        unsigned* words = (unsigned*)packet;
        int length = words[0] >> 24, count;
        unsigned list;
        int first, entries;
        /* The native room's translucent markers sit between the packets
         * (port only): step over them. */
        if (render_translucent_marker && render_translucent_marker(words, &list, &first, &entries)) {
            packet += 20;
            continue;
        }
        if (length == 9)
            count = 3;
        else if (length == 12)
            count = 4;
        else
            return;
        for (int i = 0; i < count; ++i)
            words[1 + i * 3] = tint_color(words[1 + i * 3], fade, dr, dg, db);
        packet += (count == 3 ? 0x28 : 0x34) + extra;
    }
}

/* 2EA3C.s: the room-name table is `count` 12-byte entries followed by the
 * names; turn each entry's size (word 1) into a pointer to its name and
 * return the end of the names. Like the original do-while, count must be
 * nonzero. */
void* vs_battle_initSceneAndGetRoomNames(void* table)
{
    unsigned* words = table;
    unsigned count = words[0];
    if (!count)
        pc_unimplemented("room-name table with no entries");
    unsigned char* name = (unsigned char*)(words + 1) + count * 12;
    for (unsigned i = 0; i < count; ++i) {
        unsigned* entry = (unsigned*)((unsigned char*)(words + 1) + i * 12);
        unsigned size = entry[1];
        entry[1] = psx_to_int(unsigned, name);
        name += size;
    }
    return name;
}
/* 2EA3C.s: read/write the GTE screen offset (OFX/OFY, 16.16) as integers. */
void vs_battle_getGeomOffset(unsigned* offset)
{
    offset[0] = (unsigned)CFC2(24) >> 16;
    offset[1] = (unsigned)CFC2(25) >> 16;
}
void vs_battle_setGeomOffset(unsigned* offset)
{
    CTC2(offset[0] << 16, 24);
    CTC2(offset[1] << 16, 25);
}

extern unsigned short D_800F1CDC;
extern unsigned char vs_main_stateFlags[];
/* Link `packet` (length `words`) at the head of the OT chain `head`. */
static unsigned* chain(unsigned* packet, unsigned* head, unsigned words)
{
    packet[0] = (*head & 0xffffff) | (words << 24);
    *head = (*head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
    return packet;
}
/* 2EA3C.s func_80098F8C: draw the area map. `rooms` is a count then 12-byte
 * entries (visible flag, geometry). Each room's vertices are projected into
 * scratchpad 0x100 (GTE translation 0,0,0x100); the first room (the current
 * one) also gets translucent filled triangles/quads, its edge lines and its
 * markers; every room gets outline lines. Markers are plus signs (doors and
 * points of interest, the 0x10 kind blinking with D_800F1CDC bit 5) or an
 * 8x8 icon when their state flag is clear. Filled shapes and lines link into
 * OT[5], markers into OT[4]; each list ends with a 0xE100020C draw mode. */
void func_80098F8C(void* rooms)
{
    unsigned* table = rooms;
    unsigned count = table[0];
    unsigned* ot = psx_load(unsigned*, 0x1f800004);
    unsigned* packet = psx_load(unsigned*, 0x1f800000);
    unsigned* xy = (unsigned*)0x1f800100;
    unsigned shapes = ot[5], markers = ot[4];
    unsigned blink = D_800F1CDC;
    CTC2(0, 5);
    CTC2(0, 6);
    CTC2(0x100, 7);
    for (unsigned room = 0; room < count; ++room) {
        unsigned* entry = table + 1 + room * 3;
        unsigned* data = psx_to_ptr(unsigned*, entry[1]);
        if (!entry[0] || !*data)
            continue;
        int vertices = (int)*data++;
        unsigned* out = xy;
        for (int left = vertices; left > 0; left -= 3, data += 6, out += 3) {
            MTC2(data[0], 0);
            MTC2(data[1], 1);
            MTC2(data[2], 2);
            MTC2(data[3], 3);
            MTC2(data[4], 4);
            MTC2(data[5], 5);
            gte_rtpt();
            out[0] = MFC2(12);
            out[1] = MFC2(13);
            out[2] = MFC2(14);
        }
        data = psx_to_ptr(unsigned*, entry[1]) + 1 + vertices * 2;
#define V(i) xy[(i) & 0xff]
        unsigned n = *data++;
        if (room)
            data += n;
        else
            for (; n; --n, ++data) {
                unsigned* p = chain(packet, &shapes, 4);
                p[1] = 0x223f7f00;
                p[2] = V(*data);
                p[3] = V(*data >> 8);
                p[4] = V(*data >> 16);
                packet += 5;
            }
        n = *data++;
        if (room)
            data += n;
        else
            for (; n; --n, ++data) {
                unsigned* p = chain(packet, &shapes, 5);
                p[1] = 0x2a3f7f00;
                p[2] = V(*data);
                p[3] = V(*data >> 8);
                p[4] = V(*data >> 24);
                p[5] = V(*data >> 16);
                packet += 6;
            }
        for (n = *data++; n; --n, ++data) {
            unsigned* p = chain(packet, &shapes, 3);
            p[1] = 0x42ff7f00;
            p[2] = V(*data);
            p[3] = V(*data >> 8);
            packet += 4;
        }
        if (room)
            continue;
        for (n = *data++; n; --n, ++data) {
            unsigned* p = chain(packet, &shapes, 3);
            p[1] = 0x422f5f00;
            p[2] = V(*data);
            p[3] = V(*data >> 8);
            packet += 4;
        }
        for (n = *data++; n; --n, ++data) {
            unsigned word = *data, flags = word >> 16, state = (word >> 24) & 0xff;
            unsigned at = V(word);
            int x = at & 0xffff, y = (int)at >> 16;
            if (state && !(signed char)vs_main_stateFlags[state + 0x33f]) {
                unsigned* p = chain(packet, &markers, 3);
                p[1] = 0x74808080;
                p[2] = (unsigned)((y - 4) << 16) | ((x - 3) & 0xffff);
                p[3] = 0x37f898d8;
                packet += 4;
                continue;
            }
            unsigned colour;
            if (flags & 0x10) {
                if (!(blink & 0x20))
                    continue;
                colour = 0x607f00ff;
            } else if (flags & 4) {
                colour = 0x60ff7f00;
            } else if (!(flags & 0x1f)) {
                colour = 0x607f7f7f;
            } else {
                continue;
            }
            unsigned* p = chain(packet, &markers, 3);
            p[1] = colour;
            p[2] = (unsigned)((y - 1) << 16) | (unsigned)x;
            p[3] = 0x00030001;
            packet += 4;
            p = chain(packet, &markers, 3);
            p[1] = colour;
            p[2] = (unsigned)(y << 16) | ((x - 1) & 0xffff);
            p[3] = 0x00010003;
            packet += 4;
        }
#undef V
    }
    unsigned* p = chain(packet, &shapes, 2);
    p[1] = 0xe100020c;
    p[2] = 0;
    packet += 4;
    p = chain(packet, &markers, 2);
    p[1] = 0xe100020c;
    p[2] = 0;
    packet += 4;
    ot[4] = markers;
    ot[5] = shapes;
    psx_store(0x1f800000, packet);
}
