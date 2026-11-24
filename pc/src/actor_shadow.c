/* Native translation of BATTLE/4B16C.s func_800B4B14: the three-ring
 * Gouraud shadow follows nearby terrain, then enters the original PSX OT. */
#include "common.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include "psx_types.h"
#include "draw_items.h"
#include <sys/types.h>
#include <libgte.h>
#include "gte_value.h"
extern int vs_field_sampleActorSupportHeight(SVECTOR*, int, int, int);
extern MATRIX* RotMatrixYXZ_gte(SVECTOR*, MATRIX*);
extern MATRIX* vs_core_mathPremultiplyRotationMatrix(MATRIX*, MATRIX*);
extern MATRIX* vs_core_mathScaleMatrixColumns(MATRIX*, VECTOR*);
extern int D_800F49F4; /* the platform contact: its index plus two, 0 for none */
extern unsigned D_800E9A68[], D_800E9AD4[];
extern void pc_unimplemented(const char*);
/* Native drop shadows (effect_native.c; absent in unit probes): the kept
 * triangles (index into D_800E9AD4, ordering-table bucket) of this shadow,
 * at packet `start`; returns the packet pointer past its markers, NULL when it takes none (the triangles are then draw items). */
extern unsigned* pc_shadow_native(const void* actor, const void* matrix, int light, const unsigned char* faces,
    const unsigned short* buckets, int count, unsigned* start, unsigned* ot, const unsigned* disc,
    const unsigned* disc_faces) __attribute__((weak));
extern void pc_shadow_items(const void* start, const void* end) __attribute__((weak));
/* Drop-shadow mode (lights panel): 0 auto, 1 original, 2 contact only, 3
 * off. Auto: off while the path tracer shadows the baked light around
 * characters (ptgi.c), contact-only while a light casts real shadows, else
 * the original. */
int pc_drop_shadow_mode;
extern int pc_lights_shadows_active __attribute__((weak)); /* absent in unit probes */
extern int pc_baked_shadows_active __attribute__((weak));
static int drop_shadow_mode(void)
{
    if (pc_drop_shadow_mode)
        return pc_drop_shadow_mode;
    if (&pc_baked_shadows_active && pc_baked_shadows_active)
        return 3;
    return &pc_lights_shadows_active && pc_lights_shadows_active ? 2 : 1;
}

static short slope_angle(int a, int b, unsigned radius)
{
    unsigned aa = a < 0 ? 0u - (unsigned)a : (unsigned)a;
    unsigned ab = b < 0 ? 0u - (unsigned)b : (unsigned)b;
    int angle = ratan2(aa <= ab ? a : b, radius);
    return angle >= -640 && angle < 640 ? angle : 0;
}
/* photo.h (rood.ui.hide{actors}), dev_state.c: a hidden character casts no drop shadow either. */
extern int pc_photo_hide_actors __attribute__((weak));
extern int pc_dev_actor_of(const void* p) __attribute__((weak));
extern int pc_studio_hide_bodies __attribute__((weak)); /* studio.cpp (studio.h) */
void pc_actor_shadow(D_800F4538_t* actor)
{
    if (&pc_studio_hide_bodies && (pc_studio_hide_bodies & 0x20000))
        return; /* the studio's stage takes no drop shadow (a decal on a floor it shares: it fights the floor's depth); its light casts the shadows */
    if (&pc_photo_hide_actors && pc_photo_hide_actors && pc_dev_actor_of) {
        int slot = pc_dev_actor_of(actor);
        if (slot >= 0 && (pc_photo_hide_actors & (slot == 0 ? 1 : 2)))
            return;
    }
    SVECTOR* position = &actor->unk0.position;
    int floor = vs_field_sampleActorSupportHeight(position, 0, 0, 3);
    if (position->vy > floor)
        floor = position->vy;
    unsigned radius = actor->shadowRadius;
    int extent = ((VS_XFORM(vs_xform_shadow_t)->anchorHeight - floor) >> 3) + radius;
    if (extent < 20)
        extent = 20;
    unsigned size = (unsigned)extent << 7;
    int mode = drop_shadow_mode();
    if (mode == 2)
        size = size * 9 / 16; /* contact: a small, faint disc right under the feet */
    SVECTOR slope = { 0 };
    if (D_800F49F4 > 0)
        goto flat;
    int plus = vs_field_sampleActorSupportHeight(position, radius, 0, 3);
    if (D_800F49F4 > 0)
        goto flat;
    int minus = vs_field_sampleActorSupportHeight(position, -radius, 0, 3);
    if (D_800F49F4 > 0)
        goto flat;
    if (plus != minus)
        slope.vz = slope_angle(floor - minus, plus - floor, radius);
    minus = vs_field_sampleActorSupportHeight(position, 0, -radius, 3);
    if (D_800F49F4 > 0)
        goto flat;
    plus = vs_field_sampleActorSupportHeight(position, 0, radius, 3);
    if (D_800F49F4 > 0)
        goto flat;
    if (plus != minus)
        slope.vx = slope_angle(floor - plus, minus - floor, radius);
    goto transform;
flat:
    slope = (SVECTOR) { 0 };
transform:;
    MATRIX terrain = { 0 }, rotation = { 0 };
    MATRIX* camera = &vs_view.viewMatrix;
    RotMatrixYXZ_gte(&slope, &terrain);
    vs_core_mathPremultiplyRotationMatrix(camera, &terrain);
    SVECTOR angles = *(SVECTOR*)&actor->unk0.rotationX; /* unk24, facing, unk28, unk2A */
    angles.vy += (short)actor->unk1864.unk8;
    RotMatrixYXZ_gte(&angles, &rotation);
    vs_core_mathPremultiplyRotationMatrix(&terrain, &rotation);
    unsigned scale_x = (unsigned)(int)(short)actor->unk1864.radiusExpandRate >> 1;
    unsigned scale_z = (unsigned)(int)(short)actor->unk1864.radiusShrinkRate >> 1;
    VECTOR scale = { (((size * (unsigned short)actor->unk0.scaleY) >> 12) * scale_z) >> 12,
        *(unsigned*)&actor->unk0.scaleZ, /* unk30 and unk32 */
        (((size * (unsigned short)actor->unk0.scaleX) >> 12) * scale_x) >> 12, 0 };
    vs_core_mathScaleMatrixColumns(&rotation, &scale);
    vs_gte_screen screen = vs_gte_screen_current();
    vs_gte_view camera_view = vs_gte_view_of(camera, screen);
    SVECTOR origin = { VS_XFORM(vs_xform_shadow_t)->anchorX, (short)floor, (short)VS_XFORM(vs_xform_shadow_t)->anchorZ, 0 };
    vs_gte_transform_to((VECTOR*)rotation.t, &camera_view, &origin);
    vs_gte_view view = vs_gte_view_of(&rotation, screen);
    unsigned* xy = VS_XFORM(vs_xform_shadow_t)->xy;
    unsigned* depth = VS_XFORM(vs_xform_shadow_t)->depth;
    for (unsigned i = 0; i < 18; i += 3) {
        for (unsigned j = 0; j < 3; ++j) {
            SVECTOR vertex = { (short)(D_800E9A68[i + j] & 0xffff), 0, (short)(D_800E9A68[i + j] >> 16), 0 };
            vs_gte_result r = vs_gte_project(&view, &vertex);
            xy[i + j] = r.sxy;
            depth[i + j] = r.sz;
        }
    }
    *(unsigned*)&actor->unk6FC.vx = xy[0];
    *(unsigned*)&actor->unk6FC.vz = depth[0];
    unsigned rgb = actor->unk1864.unk0;
    int light = (((rgb & 255) + ((rgb >> 8) & 255) * 2 + (rgb >> 16)) >> 2) + 64
              - (signed char)actor->unk6F2 * 4;
    if (light < 0)
        light = 0;
    if (light > 255)
        light = 255;
    if (mode == 2)
        light >>= 1;
    if (mode == 3)
        return; /* off: the anchor above is still recorded, as the game uses it */
    unsigned center = (unsigned)light * 0x010101;
    unsigned edge = (unsigned)(light >> 1) * 0x010101;
    unsigned* packet = vs_scratch.unk0;
    unsigned* ot = vs_scratch.unk4;
    unsigned* start = packet;
    unsigned char kept[24];
    unsigned short kept_bucket[24];
    unsigned kept_colour[24][2];
    unsigned char kept_index[24][3];
    int kept_count = 0;
    int base_light = light;
    for (unsigned i = 0; i < 24; ++i) {
        if (i == 8) {
            center = edge;
            edge = 0;
        }
        if (i == 16) {
            edge = center;
            center = 0;
        }
        unsigned face = D_800E9AD4[i], indices[3];
        unsigned nearest = 65535;
        for (unsigned j = 0; j < 3; ++j) {
            unsigned offset = (face >> (j * 8)) & 255;
            if ((offset & 3) || offset >= 72)
                pc_unimplemented("shadow vertex offset");
            indices[j] = offset >> 2;
            if (depth[indices[j]] < nearest)
                nearest = depth[indices[j]];
        }
        unsigned bucket = (nearest >> 2) - 2;
        if (vs_gte_nclip(xy[indices[0]], xy[indices[1]], xy[indices[2]]) > 0 || bucket >= 0x7c0)
            continue;
        kept[kept_count] = (unsigned char)i;
        kept_bucket[kept_count] = (unsigned short)bucket;
        kept_colour[kept_count][0] = center, kept_colour[kept_count][1] = edge;
        for (unsigned j = 0; j < 3; ++j)
            kept_index[kept_count][j] = (unsigned char)indices[j];
        ++kept_count;
    }
    /* The native list takes the kept triangles (no draw items); when it does not, they are items, each a semi-transparent Gouraud
     * triangle in the mode B-F placed in its bucket by a marker. */
    unsigned* native = pc_shadow_native ? pc_shadow_native(actor, &rotation, base_light, kept, kept_bucket, kept_count, start, ot,
                                              D_800E9A68, D_800E9AD4)
                                        : NULL;
    if (native)
        packet = native;
    else {
        for (int k = 0; k < kept_count; ++k) {
            int first_item = pc_frame_count();
            pc_item* item = pc_frame_add(PC_ITEM_POLY, 0);
            if (item) {
                item->verts = 3, item->flags = PC_ITEM_SHADED | PC_ITEM_SEMI, item->mode = 0x640;
                unsigned colours[3] = { kept_colour[k][0], kept_colour[k][1], kept_colour[k][1] };
                for (int j = 0; j < 3; ++j) {
                    item->x[j] = (short)xy[kept_index[k][j]], item->y[j] = (short)(xy[kept_index[k][j]] >> 16);
                    item->rgb[j][0] = (unsigned char)colours[j], item->rgb[j][1] = (unsigned char)(colours[j] >> 8),
                    item->rgb[j][2] = (unsigned char)(colours[j] >> 16);
                }
            }
            packet = pc_frame_place(first_item, pc_frame_count() - first_item, ot + kept_bucket[k], packet);
        }
        if (pc_shadow_items)
            pc_shadow_items(start, packet);
    }
    vs_scratch.unk0 = packet;
}
