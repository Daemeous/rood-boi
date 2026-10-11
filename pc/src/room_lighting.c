/* BATTLE/2EA3C.s: interpolate room lighting triangles at an actor's X/Z.
 * The original one-argument wrapper implicitly forwarded MIPS a2; the native
 * entry makes that output pointer explicit instead of relying on the ABI. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern unsigned char vs_battle_roomData[];
extern void pc_unimplemented(const char*);
static int light_area(unsigned a, unsigned b, unsigned c)
{
    MTC2(a, 12);
    MTC2(b, 13);
    MTC2(c, 14);
    gte_nclip();
    return MFC2(24);
}
int func_80098C3C(unsigned position, unsigned char* color, const unsigned* triangle)
{
    int total = light_area(triangle[2], triangle[4], triangle[3]);
    if (total <= 0)
        return 0;
    int a = light_area(position, triangle[4], triangle[3]);
    if (a < 0)
        return 0;
    int c = light_area(position, triangle[3], triangle[2]);
    if (c < 0)
        return 0;
    int b = (unsigned)total - (unsigned)a - (unsigned)c;
    if (b < 0)
        return 0;
    for (unsigned channel = 0; channel < 3; ++channel) {
        unsigned shift = channel * 8;
        unsigned sum = ((triangle[5] >> shift) & 255) * (unsigned)a
                     + ((triangle[7] >> shift) & 255) * (unsigned)c
                     + ((triangle[6] >> shift) & 255) * (unsigned)b;
        unsigned value = sum / (unsigned)total;
        if ((int)value < 0)
            value = 0;
        if (value > 255)
            value = 255;
        color[channel] = value;
    }
    return 1;
}
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
                && func_80098C3C(position, (void*)color, triangle))
                return 1;
        }
    }
    *color = fallback;
    return 0;
}
int func_80098B38(unsigned position, int layer, unsigned* color)
{
    return pc_room_light(position, layer, color, psx_load(unsigned*, vs_battle_roomData + 0x74),
        *(signed char*)vs_battle_roomData);
}
void func_8008C458(unsigned position, int unused, void* color, void* actor_position)
{
    (void)unused;
    (void)actor_position;
    func_80098B38(position, 0, color);
}
