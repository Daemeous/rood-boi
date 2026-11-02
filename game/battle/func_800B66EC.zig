//! Stub returning 4 for the BATTLE overlay's 4A0A8.c unit.

/// orig: func_800B66EC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Ignores both arguments and returns 4.
pub fn func_800B66EC(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return 4;
}
