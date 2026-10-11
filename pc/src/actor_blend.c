/* BATTLE/48C7C.s func_800B147C. Keep the GTE's signed 16-bit inputs,
 * fixed-point rounding and conditional stores, including the retained IR3
 * when a rotation's Z delta is zero in the original instruction sequence. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
extern void func_800AE980(void*, void*, int);
extern void* vs_main_memcpy(void*, const void*, unsigned);
extern void pc_unimplemented(const char*);
static void blend_vector(short* dst, const short* src, int rotation, int unsigned_x)
{
    int base[3] = { unsigned_x ? (unsigned short)dst[0] : dst[0], dst[1], dst[2] };
    int delta[3] = { (unsigned_x ? (unsigned short)src[0] : src[0]) - base[0],
        src[1] - base[1], src[2] - base[2] };
    for (unsigned i = 0; i < 3; ++i) {
        if (rotation) {
            if (delta[i] >= 4096)
                delta[i] -= 8192;
            else if (delta[i] < -4096)
                delta[i] += 8192;
        }
        if (i != 2 || !rotation || delta[i])
            MTC2(delta[i], 9 + i);
    }
    gte_gpf12();
    if (!(delta[0] | delta[1] | delta[2]))
        return;
    int dx = MFC2(25), dy = MFC2(26), dz = MFC2(27);
    if (dx | dy) {
        dst[0] = base[0] + dx;
        dst[1] = base[1] + dy;
    }
    if (dz)
        *(int*)(dst + 2) = base[2] + dz;
}
void func_800B147C(void* destination, void* source, int count, int step, int duration)
{
    if (step == duration) {
        vs_main_memcpy(destination, source, 0x60c);
        return;
    }
    if (step == (int)((unsigned)duration >> 1)) {
        func_800AE980(destination, source, count);
        return;
    }
    if (!duration || count <= 0 || count > 42)
        pc_unimplemented("invalid animation blend extent");
    MTC2((int)((unsigned)step << 12) / duration, 8);
    unsigned char* dst = destination;
    unsigned char* src = source;
    blend_vector((short*)(dst + 0x148), (short*)(src + 0x148), 0, 0);
    for (int i = 0; i < count; ++i) {
        blend_vector((short*)(dst + i * 8), (short*)(src + i * 8), 1, 0);
        blend_vector((short*)(dst + 0x150 + i * 8), (short*)(src + 0x150 + i * 8), 0, 1);
    }
}
