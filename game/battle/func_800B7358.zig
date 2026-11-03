//! Sets the unk22 field of a text box from a byte of a script command
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7358 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores the low five bits of arg0[1] in the text box selected by its top three bits.
pub fn func_800B7358(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const raw = c.vs_battle_getTextBox(@as(c_int, arg0[1] >> 5)).?;
    const box: *v.vs_battle_textBox = @alignCast(@as(*align(1) v.vs_battle_textBox, @ptrCast(raw)));
    box.unk22 = arg0[1] & 0x1F;
    return 0;
}
