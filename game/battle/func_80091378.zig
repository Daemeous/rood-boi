//! Clears two battle counters and frees the heap block held in D_800F1DD4 (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80091378 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Clears the counters at D_800F1DC0 and D_800F1DC2, then frees and clears the heap block at D_800F1DD4.
pub fn func_80091378() callconv(.c) void {
    v.g.D_800F1DC0().* = 0;
    v.g.D_800F1DC2().* = 0;
    if (v.g.D_800F1DD4().*) |block| {
        c.vs_main_freeHeap(block);
    }
    v.g.D_800F1DD4().* = null;
}
