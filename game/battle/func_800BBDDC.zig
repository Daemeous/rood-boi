//! Frees the heap block held in D_800F4BA4 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BBDDC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Frees the heap block held in D_800F4BA4.
pub fn func_800BBDDC() callconv(.c) void {
    c.vs_main_freeHeapR(v.g.D_800F4BA4().*);
}
