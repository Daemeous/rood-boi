/* Native equivalents of hand-written battle assembly that uses the GTE.
 * The original GTE register side effects are reproduced, not just results. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>

/* 7399C.s: weighted sum a * 6 + 4 * b + c + 2 * d of signed 16-bit b, c, d.
 * The original computes it with MVMVA through the rotation matrix, leaving
 * R11..R33 = (b, c, d, d >> 16, 0, ...), IR = (4, 1, 2) and MAC1..3 set. */
int func_800DC284(int a, int b, int c, int d)
{
    CTC2(((unsigned)c << 16) | ((unsigned)b & 0xffff), 0);
    CTC2((unsigned)d, 1);
    CTC2(0, 2);
    CTC2(0, 3);
    CTC2(0, 4);
    MTC2(4, 9);
    MTC2(1, 10);
    MTC2(2, 11);
    gte_mvmva(0, 0, 3, 3, 0);
    return a * 6 + (int)MFC2(25);
}

extern unsigned char _trig_table[];
extern psx_long VectorNormal(VECTOR*, VECTOR*);
/* 7B5DC.s trig lookup: interleaved 4-byte table entries, quadrant folding on
 * the (possibly reflected) angle; bits above 0xFFF are ignored, as in the
 * original's unmasked second lookup. */
static int facing_trig(unsigned angle)
{
    int value;
    if ((angle & 0x400) && !(angle & 0x3ff))
        value = 0x1000;
    else {
        unsigned offset = 0, index;
        if (angle & 0x400)
            angle = 0x800 - angle;
        index = angle & 0x1ff;
        if (angle & 0x200) {
            index = 0x200 - index;
            offset = 2;
        }
        value = *(unsigned short*)(_trig_table + offset + index * 4);
    }
    return (angle & 0x800) ? -value : value;
}
/* 7B5DC.s: cosine (Q12, clamped to +-0x1000) between the facing `angle` and
 * the XZ direction from `from` to `to`; 0x1000 when `distance` is zero. The
 * direction is normalised by func_80041AF4, the VectorNormal core. */
int func_800E437C(short* from, short* to, int angle, unsigned distance)
{
    if (!distance)
        return 0x1000;
    unsigned base = (0xc00 - angle) & 0xfff;
    int z_weight = facing_trig(base), x_weight = facing_trig(base + 0x400);
    VECTOR direction = { to[0] - from[0], 0, to[2] - from[2], 0 }, unit;
    VectorNormal(&direction, &unit);
    int result = (int)((unsigned)unit.vx * x_weight + (unsigned)unit.vz * z_weight) >> 12;
    if (result < -0x1000)
        return -0x1000;
    if (result >= 0x1000)
        return 0x1000;
    return result;
}

/* A 32-bit guest word: the battle menu state block's address. */
extern unsigned D_800EB9B8;
extern MATRIX* func_800C085C(unsigned char*, int);
extern MATRIX* RotMatrixYXZ_gte(SVECTOR*, MATRIX*);
extern MATRIX* func_80041C68(MATRIX*, MATRIX*);
extern void func_800C02A8(void);
extern void func_800C0990(void*, void*, unsigned, int);
/* 58350.s (hand-written): draw a battle menu target shape. Its points (at the
 * shape buffer D_800EB9B8 + 0x48, count 0x32) are transformed by the GTE light
 * matrix (shape scale/rotation composed at scratchpad 0x398) plus the shape
 * origin as background colour, then projected; each point with a nonzero pad
 * draws a segment from the previous point (func_800C0990) with intensity from
 * the pair's depth. Scratchpad 0x398.. holds the current and previous
 * projected point (SXY, depth / 4). */
void func_800C0B50(unsigned char* shape, unsigned color)
{
    unsigned char* buffer = psx_to_ptr(unsigned char*, D_800EB9B8);
    unsigned char* point = buffer + 0x48;
    unsigned* scratch = (unsigned*)0x1F800398;
    MATRIX* scale = func_800C085C(shape + 4, *(short*)(shape + 2));
    RotMatrixYXZ_gte((SVECTOR*)(buffer + 0x24), (MATRIX*)scratch);
    int factor = rcos(*(short*)(buffer + 0x2a) << 6);
    VECTOR scaling = { factor, factor, factor, 0 };
    ScaleMatrix((MATRIX*)scratch, &scaling);
    func_80041C68(scale, (MATRIX*)scratch);
    func_800C02A8();
    for (int i = 0; i < 5; ++i)
        CTC2(scratch[i], 8 + i);
    CTC2(*(short*)(shape + 8), 13);
    CTC2(*(short*)(shape + 0xa), 14);
    CTC2(*(short*)(shape + 0xc), 15);
    color |= 0x44000000;
    unsigned count = *(unsigned short*)(buffer + 0x32);
    int divisor = shape[6] < shape[4] ? shape[4] : shape[6];
    int bias = *(short*)(buffer + 0x20) << 1;
    while (count--) {
        MTC2(*(unsigned*)point, 0);
        MTC2(*(unsigned*)(point + 4), 1);
        gte_mvmva(1, 1, 0, 1, 0);
        unsigned x = MFC2(9), y = MFC2(10), z = MFC2(11);
        MTC2((x & 0xffff) | (y << 16), 0);
        MTC2(z, 1);
        int pad = *(short*)(point + 6);
        point += 8;
        gte_rtps();
        scratch[0] = MFC2(14);
        scratch[1] = (int)MFC2(19) >> 2;
        if (pad) {
            int sum = *(short*)(scratch + 1) + *(short*)((unsigned char*)scratch + 0xc) - bias;
            /* `div $zero` has no zero check: the R3000 then leaves LO = -1
             * for a non-negative dividend and 1 for a negative one. */
            int depth = divisor ? sum / divisor : (sum < 0 ? 1 : -1);
            func_800C0990(scratch, scratch + 2, color - ((unsigned)pad << 25), 0x20 - depth);
        }
        for (int j = 0; j < 3; ++j)
            ((unsigned short*)scratch)[4 + j] = ((unsigned short*)scratch)[j];
    }
}
