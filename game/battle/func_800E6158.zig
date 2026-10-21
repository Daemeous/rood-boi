//! Battle overlay unit 7CD68: a forwarding stub (src/BATTLE/BATTLE.PRG/7CD68.c).
const c = @import("c_7CD68");

/// orig: func_800E6158 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Forwards to func_800E6F9C, which the unit calls through `c` so the owner switch selects it.
pub fn func_800E6158() callconv(.c) void {
    c.func_800E6F9C();
}
