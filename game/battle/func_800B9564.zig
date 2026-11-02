//! Sets a text box's flags and indent from a script command (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9564 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads a short at script offset 2 and sets text box (byte 1 & 0xF)'s unk0_24 and unk2E from it and byte 1.
pub fn func_800B9564(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_s0: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 2));
    const temp_s1: *align(1) v.vs_battle_textBox = @ptrCast(c.vs_battle_getTextBox(@as(c_int, arg0[1]) & 0xF).?);

    const var_a0: c_int = if (temp_s0 < 0x2000)
        psx.conv(c_int, c.func_800BFE50(temp_s0))
    else
        0x20;

    psx.setBits(&temp_s1.unk0, "unk0_24", psx.conv(u6, var_a0));
    temp_s1.unk2E = psx.conv(i16, @as(c_int, arg0[1]) >> 4);
    return 0;
}
