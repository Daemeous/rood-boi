/* BATTLE/4B16C.s: transform cumulative bone vertex groups into the original
 * screen/depth arrays, each vertex projected through its group's matrix (gte_value.h). */
#include "psx_types.h"
#include <sys/types.h>
#include <libgte.h>
#include "gte_value.h"
extern void pc_unimplemented(const char*);
extern unsigned D_800F2458[0x1FE0 / 4];

unsigned pc_project_bone_vertices(unsigned count, const unsigned* groups,
    const SVECTOR* vertices, MATRIX* matrices, unsigned* xy, unsigned* depth)
{
    unsigned cursor = 0, visible = 0;
    vs_gte_screen screen = vs_gte_screen_current();
    for (unsigned group = 0; group < count; ++group) {
        unsigned end = groups[group] >> 16;
        if (end <= cursor || end > 768)
            pc_unimplemented("invalid actor vertex group extent");
        MATRIX* matrix = &matrices[groups[group] & 0xffff];
        vs_gte_view view = vs_gte_view_of(matrix, screen);
        /* Each vertex is projected alone: the value of a batch of three (RTPT) is the three projections. */
        for (; cursor < end; ++cursor) {
            vs_gte_result r = vs_gte_project(&view, &vertices[cursor]);
            xy[cursor] = r.sxy;
            visible |= depth[cursor] = r.sz;
        }
    }
    return visible;
}
