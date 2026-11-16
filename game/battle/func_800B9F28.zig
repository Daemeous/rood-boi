//! Battle unit 4A0A8: forwards byte 1 of its argument to vs_field_setTiledBackgroundEnabled
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9F28 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes byte 1 of arg0 to vs_field_setTiledBackgroundEnabled and returns 0.
pub fn func_800B9F28(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_field_setTiledBackgroundEnabled(@as(c_int, arg0[1]));
    return 0;
}
