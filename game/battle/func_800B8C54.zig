//! Stores one byte of a script-driven table: entry arg0[1] takes arg0[2] unless arg0[1] is 0xFF
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800B8C54 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Writes arg0[2] into D_800F4C10 at index arg0[1], skipping the 0xFF index; always returns 0.
pub fn func_800B8C54(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const index: u8 = arg0[1];
    if (index != 0xFF) {
        v.g.D_800F4C10()[index] = arg0[2];
    }
    return 0;
}
