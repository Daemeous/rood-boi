/* Native equivalent of hand-written BATTLE/BATTLE.PRG/5A624.s vs_battle_drawSphereTerrainIntersection:
 * the area Map screen renderer called by vs_battle_updateSphereTargetPosition. Not a matching
 * decompilation claim; it reproduces the original memory effects literally.
 *
 * Scratchpad work space (all left behind as the original leaves it):
 *   0x1F800088  9x9 cell heights for the current view rotation (+3 pad bytes)
 *   0x1F8000DC  per-scanline span counts (224 rows)
 *   0x1F8001BC  180 cell-edge flags, then their row bytes at +0xB4; later
 *               reused for triangle span rows and the 32-point view circle
 * the battle menu block (D_800EB9B8_t.spans) holds 224 rows of 32 x-coordinates (span pairs). The
 * spans are built from the projected cell outlines, cut by lowered walls and
 * marker triangles (+0x4AB8), clipped to the circle's left edge, then emitted
 * as semi-transparent LINE_F2 packets along the circle's right edge. */
#include "common.h"
#include "widescreen.h"
#include <libgte.h>
#include <inline_c.h>
#include "pc/src/game/BATTLE/BATTLE.PRG/target_block.h"

/* The battle menu's block (target_block.h). */
extern D_800EB9B8_t* vs_battle_targetSphere;

extern void pc_unimplemented(const char*);
/* A 32-bit guest word: the battle menu state block's address. */
extern u_short D_800EA130[]; /* 0x8000 / n; indexed past its end as the original */
extern u_short D_800EA6B0[]; /* edge -> pair of neighbouring cell indices */
extern short D_800EA818[];   /* 40-entry sine table: sin at i, cos at i + 8 */

/* The rows' spans as emitted (for the hardware renderer's shapes, gpu_hw.c
 * pc_hw_span_shape; host memory only, nothing of the game's). */
extern void pc_hw_span_shape(const void* packet, int length, float top_left, float top_right, float bottom_left,
    float bottom_right) __attribute__((weak));
#define PC_MAP_EMITTED 1024
static struct {
    int item; /* the line's draw item */
    short row, x0, x1; /* x1 inclusive */
} pc_map_emitted[PC_MAP_EMITTED] __attribute__((used));
static int pc_map_emitted_count __attribute__((used));

/* Each emitted row joined to the rows above and below: where a span and a
 * span of the next row overlap the most for both, their ends meet half way
 * between the rows (a projected edge passes through the rows' ends), else
 * the row keeps its ends there. */
/* Triangle/circle row buffers live after the edge flags; the PS1 scratchpad
 * ends at 0x1F800400 and the original never checks. */
/* 5A624.s 9x9 cell pass: unreachable/too-high cells become 0, floors visible
 * from height y get min(y - floor, 0x30), others keep the preset 1. */
/* Remove [left, right] from the first span of a row it touches (cases at
 * 0x800C3690 and 0x800C3F58): drop, trim either end, or split the span. */
/* Wall cuts against a lowered edge at x = p with wall limit lim.
 * mode 0 (0x800C389C/0x800C39A4): [x0,x1] -> [x0,lim][p,x1] or [p,x1].
 * mode 1 (0x800C3AA8): [x0,x1] -> [x0,p][lim,x1] or [x0,p], needs x0 < p.
 * mode 2 (0x800C3BAC): as mode 1 but accepts x0 == p. */
void vs_battle_drawSphereTerrainIntersection(SVECTOR* origin, MATRIX* m, int scale);

