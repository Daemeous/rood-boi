//! Sets the unk22 field of a text box from a byte of a script command
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800B7358 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores the low five bits of arg0[1] in the text box selected by its top three bits.
pub fn func_800B7358(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const box = c.vs_battle_getTextBox(@as(c_int, arg0[1] >> 5)).?;
    // unk22 is the byte at offset 0x22; vs_battle_textBox is not in views_4A0A8 yet.
    const field: *align(1) u8 = @ptrFromInt(@intFromPtr(box) +% 0x22);
    field.* = arg0[1] & 0x1F;
    return 0;
}
