/* Native room submission from BATTLE/2EA3C.s: the original packed geometry and its visibility and ordering-table decisions are
 * kept; no room packets are built (the native list draws the opaque polygons, a marker each semi-transparent one). */
#include <sys/types.h>
#include <libgte.h>
#include "gte_value.h"

extern void pc_unimplemented(const char*);
extern int pc_room_quad_visible(const unsigned[4]);
extern int pc_room_depth_bucket(unsigned, int);
extern int pc_room_within_distance(unsigned);
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
static int projected_area(unsigned a, unsigned b, unsigned c)
{
    return vs_gte_nclip(a, b, c);
}
/* room_native.c: a translucent marker in front of each semi-transparent
 * polygon's packet (the native list draws it there). */
extern unsigned* pc_room_native_translucent(unsigned index, unsigned* packet, unsigned* ot, int bucket, int halve,
    const unsigned* limit) __attribute__((weak));
extern void pc_room_render_rules(int* near_bucket, int* far_bucket, int* lod_bucket);
static int native __attribute__((used));
/* The semi-transparent polygons of one room group (the native list draws the opaque ones): each is decided as the game's builder
 * did (back-face, visibility, ordering-table bucket) and drawn by a translucent marker linked at its bucket; no packet is written.
 * Without the native room (no hardware path) nothing is drawn. */
unsigned* pc_room_group_packets(unsigned* geometry, unsigned* packet, unsigned* ot,
    unsigned gp, unsigned mode, unsigned* limit, const vs_gte_view* view)
{
    unsigned triangles = geometry[0], quads = geometry[1];
    unsigned* poly = geometry + 2;
    if (triangles > 65535 || quads > 65535)
        pc_unimplemented("room polygon count");
    if (!native || !pc_room_native_translucent)
        return packet;
    for (unsigned i = 0; i < triangles + quads; ++i) {
        int quad = i >= triangles;
        if (!(poly[3] & 0x02000000))
            goto next;
        SVECTOR vertex[4];
        unsigned xy[4], sz[4];
        decode_vertices(poly, gp & 31, vertex, quad);
        vs_gte_result p0 = vs_gte_project(view, &vertex[0]);
        vs_gte_result p1 = vs_gte_project(view, &vertex[1]);
        vs_gte_result p2 = vs_gte_project(view, &vertex[2]);
        xy[0] = p0.sxy;
        xy[1] = p1.sxy;
        xy[2] = xy[3] = p2.sxy;
        sz[0] = p0.sz;
        sz[1] = p1.sz;
        sz[2] = p2.sz;
        unsigned depth = sz[0], z = sz[1];
        if (z > depth)
            depth = z;
        z = p2.sz;
        if (z > depth)
            depth = z;
        int area = projected_area(xy[0], xy[1], xy[2]);
        if (!quad && area < 0 && !(poly[3] & 0x01000000))
            goto next;
        if (!quad && area < 0)
            area = -area;
        if (quad) {
            vs_gte_result p3 = vs_gte_project(view, &vertex[3]);
            xy[3] = p3.sxy;
            z = sz[3] = p3.sz;
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
            /* (a near, large polygon the game splits into leaves: one marker for the whole polygon, where its farthest
             * leaves can be) */
            int near_bucket, far_bucket, lod_bucket;
            pc_room_render_rules(&near_bucket, &far_bucket, &lod_bucket);
            if ((int)(depth >> 2) >= near_bucket)
                packet = pc_room_native_translucent(i, packet, ot, (int)(depth >> 2) + ((int)gp >> 16), 0, limit);
            goto next;
        }
        int bucket = pc_room_depth_bucket(depth, (int)gp >> 16);
        if (bucket < 0)
            goto next;
        packet = pc_room_native_translucent(i, packet, ot, bucket, !pc_room_within_distance(bucket), limit);
    next:
        poly += quad ? 10 : 8;
    }
    return packet;
}
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
 * "drawn", facing_cutaway_smoke.py): the same whether the native room draws or not,
 * since this function decides for both paths. */
#define DRAWN_MAX 4096
static const void* drawn_data __attribute__((used));
static unsigned drawn_count __attribute__((used)), drawn_facing __attribute__((used));
static unsigned char drawn_groups[DRAWN_MAX] __attribute__((used));
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
void vs_render_drawRoomGeometry(void* data, unsigned mode);


/* 2EA3C.s vs_render_tintRoomGeometryPrimitives: tint the room (game/battle/pc/tint_room_packets.zig). */
void vs_render_tintRoomGeometryPrimitives(unsigned* start, unsigned* end, signed char* params, int flags);


/* 2EA3C.s: the room-name table is `count` 12-byte entries followed by the
 * names; turn each entry's size (word 1) into a pointer to its name and
 * return the end of the names. Like the original do-while, count must be
 * nonzero. */
void* vs_battle_initSceneAndGetRoomNames(void* table);

void vs_battle_getGeomOffset(unsigned* offset);

void vs_battle_setGeomOffset(unsigned* offset);


extern unsigned short vs_field_roomAnimationTicks;
extern unsigned char vs_main_stateFlags[1088]; /* vs_main_flags_t */
/* 2EA3C.s vs_field_drawMinimapScene: draw the area map. `rooms` is a count then 12-byte
 * entries (visible flag, geometry). Each room's vertices are projected into
 * scratchpad 0x100 (GTE translation 0,0,0x100); the first room (the current
 * one) also gets translucent filled triangles/quads, its edge lines and its
 * markers; every room gets outline lines. Markers are plus signs (doors and
 * points of interest, the 0x10 kind blinking with vs_field_roomAnimationTicks bit 5) or an
 * 8x8 icon when their state flag is clear. Filled shapes and lines link into
 * OT[5], markers into OT[4]; each list ends with a 0xE100020C draw mode. */
void vs_field_drawMinimapScene(void* rooms, const vs_gte_view* view); /* view: the map's rotation and projection; its translation is set to (0, 0, 0x100) */

