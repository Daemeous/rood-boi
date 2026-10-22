//! Copies the battle pointer D_800F5698 into D_800F569C (src/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: func_800D78CC (src/BATTLE/BATTLE.PRG/6F014.c)
/// Copies the pointer held in D_800F5698 into D_800F569C.
pub fn func_800D78CC() callconv(.c) void {
    v.g.D_800F569C().* = v.g.D_800F5698().*;
}
