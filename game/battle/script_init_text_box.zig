//! The event script's text-box setup (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_initTextBox (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens text box script[1] & 0xF at (script[4] * 2 - 32, script[5] - 32), script[6] characters by
/// script[7] lines, growing from its own centre or from parent box script[1] >> 4's; sets its unk2E
/// and, for a reference below 0x2000, its actor.
pub fn script_initTextBox(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var center_x: i16 = undefined;
    var center_y: i16 = undefined;
    if ((script[1] & 0xF) == (script[1] >> 4)) {
        center_x = 32;
        center_x = psx.conv(i16, @as(c_int, script[4]) * 2 + ((((@as(c_int, script[6]) * 12) + 8) >> 1) - center_x)); // bytes: no overflow
        center_y = psx.conv(i16, @as(c_int, script[5]) + 0xFFE0 + (((@as(c_int, script[7]) * 13) + 4) >> 1));
    } else {
        const parent: *v.vs_battle_textBox = @ptrCast(@alignCast(c.vs_battle_getTextBox(script[1] >> 4).?));
        center_x = psx.conv(i16, @as(c_int, parent.x) +% 4);
        center_x = psx.conv(i16, @as(c_int, center_x) +% @as(c_int, parent.charsPerLine) * 6);
        center_y = psx.conv(i16, @as(c_int, parent.y) +% (((@as(c_int, parent.lineCount) * 13) + 4) >> 1));
    }
    c.vs_battle_initTextBox(script[1] & 0xF, c.vs_battle_getShort(script + 2), @as(c_int, script[4]) * 2 - 32,
        @as(c_int, script[5]) - 32, script[6], script[7], center_x, center_y);
    const box: *v.vs_battle_textBox = @ptrCast(@alignCast(c.vs_battle_getTextBox(script[1] & 0xF).?));
    box.unk2E = script[10];
    const ref = c.vs_battle_getShort(&script[8]);
    if (ref < 0x2000) {
        psx.setBits(&box.unk0, "unk0_24", psx.conv(u6, c.func_800BFE50(psx.conv(u16, ref))));
    }
    return 0;
}
