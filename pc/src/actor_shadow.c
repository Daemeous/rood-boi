/* Native translation of BATTLE/4B16C.s func_800B4B14: the three-ring
 * Gouraud shadow follows nearby terrain, then enters the original PSX OT. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern int func_800A6EE8(SVECTOR*, int, int, int);
extern MATRIX* RotMatrixYXZ_gte(SVECTOR*, MATRIX*);
extern MATRIX* func_800B196C(MATRIX*, MATRIX*);
extern MATRIX* func_800B396C(MATRIX*, VECTOR*);
extern signed char D_800F49F4;
extern unsigned D_800E9A68[], D_800E9AD4[];
extern void pc_unimplemented(const char*);
/* Native drop shadows (effect_native.c; absent in unit probes): the kept
 * triangles (index into D_800E9AD4, ordering-table bucket) of this shadow,
 * whose packets are [start, end); returns the new packet pointer. */
extern unsigned* pc_shadow_native(const void* actor, const void* matrix, int light, const unsigned char* faces,
    const unsigned short* buckets, int count, unsigned* start, unsigned* end, unsigned* ot, const unsigned* disc,
    const unsigned* disc_faces) __attribute__((weak));
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
void pc_actor_shadow(unsigned char* actor, unsigned char* scratch)
{
    if (&pc_photo_hide_actors && pc_photo_hide_actors && pc_dev_actor_of) {
        int slot = pc_dev_actor_of(actor);
        if (slot >= 0 && (pc_photo_hide_actors & (slot == 0 ? 1 : 2)))
            return;
    }
    SVECTOR* position = (SVECTOR*)(actor + 0x1c);
    int floor = func_800A6EE8(position, 0, 0, 3);
    if (position->vy > floor)
        floor = position->vy;
    unsigned radius = *(unsigned short*)(actor + 0x64c);
    int extent = ((*(short*)(scratch + 0x3b2) - floor) >> 3) + radius;
    if (extent < 20)
        extent = 20;
    unsigned size = (unsigned)extent << 7;
    int mode = drop_shadow_mode();
    if (mode == 2)
        size = size * 9 / 16; /* contact: a small, faint disc right under the feet */
    SVECTOR slope = { 0 };
    if (D_800F49F4 > 0)
        goto flat;
    int plus = func_800A6EE8(position, radius, 0, 3);
    if (D_800F49F4 > 0)
        goto flat;
    int minus = func_800A6EE8(position, -radius, 0, 3);
    if (D_800F49F4 > 0)
        goto flat;
    if (plus != minus)
        slope.vz = slope_angle(floor - minus, plus - floor, radius);
    minus = func_800A6EE8(position, 0, -radius, 3);
    if (D_800F49F4 > 0)
        goto flat;
    plus = func_800A6EE8(position, 0, radius, 3);
    if (D_800F49F4 > 0)
        goto flat;
    if (plus != minus)
        slope.vx = slope_angle(floor - plus, minus - floor, radius);
    goto transform;
flat:
    slope = (SVECTOR) { 0 };
transform:;
    MATRIX terrain = { 0 }, rotation = { 0 };
    MATRIX* camera = (MATRIX*)(scratch + 0x14);
    RotMatrixYXZ_gte(&slope, &terrain);
    func_800B196C(camera, &terrain);
    SVECTOR angles = *(SVECTOR*)(actor + 0x24);
    angles.vy += *(short*)(actor + 0x186c);
    RotMatrixYXZ_gte(&angles, &rotation);
    func_800B196C(&terrain, &rotation);
    unsigned scale_x = (unsigned)(int)*(short*)(actor + 0x1870) >> 1;
    unsigned scale_z = (unsigned)(int)*(short*)(actor + 0x1872) >> 1;
    VECTOR scale = { (((size * *(unsigned short*)(actor + 0x2e)) >> 12) * scale_z) >> 12,
        *(unsigned*)(actor + 0x30),
        (((size * *(unsigned short*)(actor + 0x2c)) >> 12) * scale_x) >> 12, 0 };
    func_800B396C(&rotation, &scale);
    SetRotMatrix(camera);
    SetTransMatrix(camera);
    MTC2(
        *(unsigned short*)(scratch + 0x3b0) | ((unsigned)(unsigned short)floor << 16), 0);
    MTC2(*(unsigned*)(scratch + 0x3b4), 1);
    gte_rt();
    rotation.t[0] = MFC2(25);
    rotation.t[1] = MFC2(26);
    rotation.t[2] = MFC2(27);
    SetRotMatrix(&rotation);
    SetTransMatrix(&rotation);
    unsigned* xy = (unsigned*)(scratch + 0x370);
    unsigned* depth = (unsigned*)(scratch + 0x3b8);
    for (unsigned i = 0; i < 18; i += 3) {
        for (unsigned j = 0; j < 3; ++j) {
            MTC2(D_800E9A68[i + j] & 0xffff, j * 2);
            MTC2(D_800E9A68[i + j] >> 16, j * 2 + 1);
        }
        gte_rtpt();
        for (unsigned j = 0; j < 3; ++j) {
            xy[i + j] = MFC2(12 + j);
            depth[i + j] = MFC2(17 + j);
        }
    }
    *(unsigned*)(actor + 0x6fc) = xy[0];
    *(unsigned*)(actor + 0x700) = depth[0];
    unsigned rgb = *(unsigned*)(actor + 0x1864);
    int light = (((rgb & 255) + ((rgb >> 8) & 255) * 2 + (rgb >> 16)) >> 2) + 64
              - *(signed char*)(actor + 0x6f2) * 4;
    if (light < 0)
        light = 0;
    if (light > 255)
        light = 255;
    if (mode == 2)
        light >>= 1;
    if (mode == 3)
        return; /* off: the anchor above is still recorded, as the game uses it */
    unsigned center = 0x32000000 | (unsigned)light * 0x010101;
    unsigned edge = (unsigned)(light >> 1) * 0x010101;
    unsigned* packet = psx_load(unsigned*, scratch);
    unsigned* ot = psx_load(unsigned*, scratch + 4);
    unsigned* start = packet;
    unsigned char kept[24];
    unsigned short kept_bucket[24];
    int kept_count = 0;
    int base_light = light;
    for (unsigned i = 0; i < 24; ++i) {
        if (i == 8) {
            center = 0x32000000 | edge;
            edge = 0;
        }
        if (i == 16) {
            edge = center;
            center = 0x32000000;
        }
        unsigned face = D_800E9AD4[i], indices[3];
        unsigned nearest = 65535;
        for (unsigned j = 0; j < 3; ++j) {
            unsigned offset = (face >> (j * 8)) & 255;
            if ((offset & 3) || offset >= 72)
                pc_unimplemented("shadow vertex offset");
            indices[j] = offset >> 2;
            MTC2(xy[indices[j]], 12 + j);
            if (depth[indices[j]] < nearest)
                nearest = depth[indices[j]];
        }
        gte_nclip();
        unsigned bucket = (nearest >> 2) - 2;
        if ((int)MFC2(24) > 0 || bucket >= 0x7c0)
            continue;
        packet[0] = 0x07000000 | (ot[bucket] & 0xffffff);
        packet[1] = 0xe1000640;
        packet[2] = center;
        packet[3] = xy[indices[0]];
        packet[4] = edge;
        packet[5] = xy[indices[1]];
        packet[6] = edge;
        packet[7] = xy[indices[2]];
        ot[bucket] = (ot[bucket] & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
        packet += 8;
        kept[kept_count] = (unsigned char)i;
        kept_bucket[kept_count++] = (unsigned short)bucket;
    }
    if (pc_shadow_native)
        packet = pc_shadow_native(actor, &rotation, base_light, kept, kept_bucket, kept_count, start, packet, ot,
            D_800E9A68, D_800E9AD4);
    psx_store(scratch, packet);
}
void func_800B4B14(void* actor) { pc_actor_shadow(actor, (void*)0x1f800000); }
