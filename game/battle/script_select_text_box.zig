//! Script opcode: opens a text box and waits for the player's selection (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

extern fn vs_battle_setStateFlag(c_int, c_int) void;

/// orig: vs_battle_script_selectTextBox (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens text box `script[1]` on the first call and, on later calls, finishes once a selection is made.
pub fn script_selectTextBox(script: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.D_800F4C2C().* == 2) return 0;

    if (v.g._textBoxSelectionPending().* != 0) {
        const temp: c_short = psx.conv(c_short, c.vs_battle_selectTextBox(script[1]));
        if ((@as(c_int, temp) << 16) != 0) {
            vs_battle_setStateFlag(0x10, @as(c_int, temp));
            v.g._textBoxSelectionPending().* = 0;
            return 0;
        }
    } else {
        _ = c.vs_battle_selectTextBox(script[1]);
        v.g._textBoxSelectionPending().* = 1;
    }
    return 1;
}
