//! Frees the heap block in vs_battle_targetSphere and clears the pointer (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800C05B4 (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Frees the block held in vs_battle_targetSphere, if any, and clears the pointer.
pub fn func_800C05B4() callconv(.c) void {
    const heap = v.g.vs_battle_targetSphere();
    if (heap.*) |block| {
        c.vs_main_freeHeap(block);
        heap.* = null;
    }
}
