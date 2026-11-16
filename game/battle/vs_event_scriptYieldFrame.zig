//! Stub returning 4 for the BATTLE overlay's 4A0A8.c unit.

/// orig: vs_event_scriptYieldFrame (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Ignores both arguments and returns 4.
pub fn vs_event_scriptYieldFrame(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return 4;
}
