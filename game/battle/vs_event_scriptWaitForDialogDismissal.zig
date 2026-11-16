//! Dismisses the text box named by byte 1 of the argument (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_scriptWaitForDialogDismissal (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Dismisses the text box named by `arg0[1]`; returns 1 when the dismiss returned nonzero, else 0.
pub fn vs_event_scriptWaitForDialogDismissal(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    return @as(c_int, @intFromBool(c.vs_battle_dismissTextBox(@as(c_int, arg0[1])) != 0));
}
