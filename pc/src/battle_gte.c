/* Native equivalents of hand-written battle assembly that uses the GTE.
 * The original GTE register side effects are reproduced, not just results. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
#include "pc/src/game/BATTLE/BATTLE.PRG/target_block.h"

/* The battle menu's block (target_block.h). */
extern D_800EB9B8_t* vs_battle_targetSphere;

/* 7399C.s: weighted sum a * 6 + 4 * b + c + 2 * d of signed 16-bit b, c, d.
 * The original computes it with MVMVA through the rotation matrix, leaving
 * R11..R33 = (b, c, d, d >> 16, 0, ...), IR = (4, 1, 2) and MAC1..3 set. */
int vs_battle_aiScoreActorCondition(int a, int b, int c, int d);


extern unsigned char _trig_table[];
extern psx_long VectorNormal(VECTOR*, VECTOR*);
/* 7B5DC.s trig lookup: interleaved 4-byte table entries, quadrant folding on
 * the (possibly reflected) angle; bits above 0xFFF are ignored, as in the
 * original's unmasked second lookup. */
/* 7B5DC.s: cosine (Q12, clamped to +-0x1000) between the facing `angle` and
 * the XZ direction from `from` to `to`; 0x1000 when `distance` is zero. The
 * direction is normalised by vs_core_mathNormalizeVector, the VectorNormal core. */
int vs_battle_aiTargetFacingCosine(short* from, short* to, int angle, unsigned distance);


extern MATRIX* func_800C085C(unsigned char*, int);
extern MATRIX* RotMatrixYXZ_gte(SVECTOR*, MATRIX*);
extern MATRIX* vs_core_mathMultiplyMatrixIntoRight(MATRIX*, MATRIX*);
extern void vs_render_loadFrameViewMatrix(void);
extern void func_800C0990(void*, void*, unsigned, int);
/* 58350.s (hand-written): draw a battle menu target shape. Its points (at the
 * D_800EB9B8_t.points, count 0x32) are transformed by the GTE light
 * matrix (shape scale/rotation composed at scratchpad 0x398) plus the shape
 * origin as background colour, then projected; each point with a nonzero pad
 * draws a segment from the previous point (func_800C0990) with intensity from
 * the pair's depth. Scratchpad 0x398.. holds the current and previous
 * projected point (SXY, depth / 4). */
void vs_battle_drawSphereVolumeWireframe(unsigned char* shape, unsigned color);

