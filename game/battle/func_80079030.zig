//! The action's phase 8: the re-cast (src/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_80079030 (src/BATTLE/BATTLE.PRG/146C.c)
/// Starts the cast again (func_8007138C).
pub fn func_80079030() callconv(.c) void {
    c.func_8007138C();
}
