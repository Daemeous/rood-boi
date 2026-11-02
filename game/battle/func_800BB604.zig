//! Battle-end check: counts the text boxes still open after dismissing each of the eight
//! slots (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BB604 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Dismisses text boxes 0..7; returns 2 and calls func_8007C36C(2) when none was open, else 1.
pub fn func_800BB604(_: [*c]u8, _: c_short) callconv(.c) c_int {
    var open_count: c_int = 0;
    var i: c_int = 0;
    while (i < 8) : (i +%= 1) {
        if (c.vs_battle_dismissTextBox(i) != 0) {
            open_count +%= 1;
        }
    }

    if (open_count == 0) {
        _ = c.func_8007C36C(2);
        return 2;
    }
    return 1;
}
