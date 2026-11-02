//! The action's phase 6: waiting for the cast flash (src/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: func_800787F0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Once the cast flash (D_800F1868 bit 1) is over, releases the action (func_80070278) and calls func_800CEF74(0).
pub fn func_800787F0() callconv(.c) void {
    if (v.g.D_800F1868().* & 2 == 0) {
        c.func_80070278();
        _ = c.func_800CEF74(0);
    }
}
