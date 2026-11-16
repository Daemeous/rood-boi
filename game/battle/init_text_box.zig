//! Sets up a text box's geometry, flags and layout for one of the battle text boxes
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_initTextBox (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Resets text box `id` at (x, y) with its flags, size and centre, and applies the layout of box kinds 1, 4 and 5.
pub fn initTextBox(id: c_int, flags: c_int, x: c_int, y: c_int, charsPerLine: c_int, h: c_int, centerX: c_int, centerY: c_int) callconv(.c) void {
    const box = psx.at(v.g.textBoxes(), id);

    // *(int*)&box->unk0 = flags & 0xFFFF;
    psx.byteAt(c_int, box, 0).* = flags & 0xFFFF;
    psx.setBits(&box.flags, "unk0_24", 32);
    box.x = psx.conv(i16, x);
    box.y = psx.conv(i16, y);
    box.unk4 = 0;
    box.unk6 = 0;
    box.speed = 0;
    box.xIndent = psx.conv(i16, x +% 5);
    box.yIndent = psx.conv(i16, y +% 3);
    box.unk2E = 0;
    box.brightness = 0x80;
    box.charsPerLine = psx.conv(u8, charsPerLine);
    box.lineCount = psx.conv(u8, h);
    box.centerX = psx.conv(i16, centerX);
    box.centerY = psx.conv(i16, centerY);

    // Shift amount is unk0_4 * 4 in {0, 4, 8, 12}: always below 32.
    const shift: c_int = @as(c_int, psx.getBits(&box.flags, "unk0_4")) *% 4;
    box.unk22 = psx.conv(u8, ((@as(c_int, 0x45A5) >> @as(u5, @intCast(shift))) & 0xF) *% 2);

    if (box.state <= 0) {
        box.state = 1;
    }

    switch (@as(c_int, psx.getBits(&box.flags, "unk0_0"))) {
        1 => {
            psx.setBits(&box.flags, "unk0_0", 7);
            psx.setBits(&box.flags, "unk0_8", 0);
        },
        4 => {
            box.charsPerLine = 0x18;
            box.centerX = 0xA0;
            box.centerY = 0xF0;
            box.xIndent = 0x10;
            box.yIndent = psx.conv(i16, @as(c_int, 0xF2) -% (h *% 0xD));
        },
        5 => {
            psx.setBits(&box.flags, "unk0_0", 7);
            psx.setBits(&box.flags, "unk0_8", 2);
        },
        else => {},
    }
}
