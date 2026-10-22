//! Battle overlay unit 6F014: copies one block pointer global into another (src/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: func_800D78B8 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Copies the pointer in D_800F569C into D_800F5698.
pub fn func_800D78B8() callconv(.c) void {
    v.g.D_800F5698().* = v.g.D_800F569C().*;
}
