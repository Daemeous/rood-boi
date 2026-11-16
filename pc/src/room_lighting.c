/* BATTLE/2EA3C.s: interpolate room lighting triangles at an actor's X/Z.
 * The original one-argument wrapper implicitly forwarded MIPS a2; the native
 * entry makes that output pointer explicit instead of relying on the ABI. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
/* dev_state.c: the room's lighting triangle table, and whether the room's lighting is enabled (its signed first byte) */
extern const unsigned* pc_room_light_table(int* enabled);
extern void pc_unimplemented(const char*);
int vs_render_interpolateRoomLightTriangle(unsigned position, unsigned char* color, const unsigned* triangle);

int pc_room_light(
    unsigned position, int layer, unsigned* color, const unsigned* table, int enabled)
{
    unsigned fallback = 0x808080;
    if (enabled && table) {
        fallback = table[0];
        unsigned count = table[layer ? 2 : 1];
        const unsigned* triangle = table + 3 + (layer ? table[1] * 8 : 0);
        if (count > 4096)
            pc_unimplemented("room light triangle count");
        int x = (short)position, z = (short)(position >> 16);
        for (unsigned i = 0; i < count; ++i, triangle += 8) {
            if (x >= (short)triangle[0] && x <= (short)triangle[1]
                && z >= (short)(triangle[0] >> 16) && z <= (short)(triangle[1] >> 16)
                && vs_render_interpolateRoomLightTriangle(position, (void*)color, triangle))
                return 1;
        }
    }
    *color = fallback;
    return 0;
}
int vs_render_sampleRoomLighting(unsigned position, int layer, unsigned* color);

void func_8008C458(unsigned position, int unused, void* color, void* actor_position)
{
    (void)unused;
    (void)actor_position;
    vs_render_sampleRoomLighting(position, 0, color);
}
