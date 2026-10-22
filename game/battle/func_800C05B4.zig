//! Frees the heap block in D_800EB9B8 and clears the pointer (src/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800C05B4 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Frees the block held in D_800EB9B8, if any, and clears the pointer.
pub fn func_800C05B4() callconv(.c) void {
    const heap = v.g.D_800EB9B8();
    if (heap.*) |block| {
        c.vs_main_freeHeap(block);
        heap.* = null;
    }
}
