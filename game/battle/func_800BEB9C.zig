//! Clears the battle state flags 64 to 127 when called with 0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BEB9C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears state flags 64 to 127 when called with 0.
pub fn func_800BEB9C(arg0: c_int) callconv(.c) void {
    if (arg0 == 0) {
        var i: i16 = 64;
        // i < 128 bounds the loop, so the increment cannot leave i16.
        while (i < 128) : (i +%= 1) {
            _ = c.vs_battle_setStateFlag(i, 0);
        }
    }
}
