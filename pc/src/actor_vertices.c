/* BATTLE/4B16C.s: transform cumulative bone vertex groups into the original
 * screen/depth arrays. Match RTPT batches and the RTPS single-vertex tail. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void pc_unimplemented(const char*);
extern unsigned D_800F2458[];

unsigned pc_project_bone_vertices(unsigned count, const unsigned* groups,
    const SVECTOR* vertices, MATRIX* matrices, unsigned* xy, unsigned* depth)
{
    unsigned cursor = 0, visible = 0;
    for (unsigned group = 0; group < count; ++group) {
        unsigned end = groups[group] >> 16;
        if (end <= cursor || end > 768)
            pc_unimplemented("invalid actor vertex group extent");
        MATRIX* matrix = &matrices[groups[group] & 0xffff];
        SetRotMatrix(matrix);
        SetTransMatrix(matrix);
        while (cursor < end) {
            unsigned batch = end - cursor;
            if (batch > 3)
                batch = 3;
            for (unsigned i = 0; i < batch; ++i) {
                const SVECTOR* v = &vertices[cursor + i];
                MTC2((unsigned short)v->vx | ((unsigned)(unsigned short)v->vy << 16),
                    i * 2);
                MTC2((unsigned short)v->vz | ((unsigned)(unsigned short)v->pad << 16),
                    i * 2 + 1);
            }
            if (batch == 1) {
                gte_rtps();
                xy[cursor] = MFC2(14);
                visible |= depth[cursor] = MFC2(19);
            } else {
                gte_rtpt();
                for (unsigned i = 0; i < batch; ++i) {
                    xy[cursor + i] = MFC2(12 + i);
                    visible |= depth[cursor + i] = MFC2(17 + i);
                }
            }
            cursor += batch;
        }
    }
    return visible;
}
/* actor_native.c: the model and bone matrices of the packets built next. */
extern void pc_object_native_matrices(const void* object, const void* matrices, const void* model)
    __attribute__((weak));
int func_800B3A68(void* object, MATRIX* matrices, int near_clip, void* model)
{
    (void)near_clip; /* The original assembly never reads this argument. */
    if (pc_object_native_matrices)
        pc_object_native_matrices(object, matrices, model);
    return pc_project_bone_vertices(((unsigned char*)object)[1],
        psx_load(unsigned*, (char*)model + 0x34), psx_load(SVECTOR*, (char*)model + 0x38), matrices,
        D_800F2458, D_800F2458 + 768);
}
