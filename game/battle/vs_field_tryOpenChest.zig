//! Room section key and sigil handling (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_tryOpenChest (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Opens a key or sigil room section when its item is held, sends its bounds to func_8009E070 and calls func_8008D5FC on the section when it is done.
pub fn vs_field_tryOpenChest(arg0: ?*v._mpdRoomSectionA, arg1: c_int) callconv(.c) void {
    _ = arg1;
    const section = arg0.?;
    // Uninitialised in the original: zero-initialised here.
    var sp10: [4]c_short = .{ 0, 0, 0, 0 };
    const shift: u5 = 7;

    sp10[0] = psx.conv(c_short, (@as(c_int, section.unk0.unk0) << shift) +% 0x40);
    sp10[2] = psx.conv(c_short, (@as(c_int, section.unk0.unk2) << shift) +% 0x40);
    var var_s1: c_int = 0;

    sw: switch (section.keyOrSigil) {
        0 => {
            var_s1 = 1;
        },
        1, 3 => {
            if (c.vs_battle_getStateFlag(psx.conv(c_short, @as(c_int, section.unkE) +% 0x1B6)) == 0) {
                if (c.vs_battle_itemIdIsInInventory(@as(c_int, section.unkE)) == 0) break :sw;
                c.vs_battle_setStateFlag(psx.conv(c_short, @as(c_int, section.unkE) +% 0x1B6), 1);
                if (@as(c_int, section.unkE) >= 0x1D2) {
                    _ = c.vs_battle_decreaseMiscCount(@as(c_int, section.unkE));
                }
            }
            section.keyOrSigil = 0;
            c.vs_battle_displaySceneMessage(4, @as(c_int, section.unkE), 1);
            var_s1 = 1;
        },
        else => {},
    }

    if (var_s1 != 0) {
        c.func_8009E070(0, &sp10, 2);
        c.func_8008D5FC(psx.cptr(c._mpdRoomSectionA, section));
        return;
    }
    c.func_8009E070(0, &sp10, 3);
}
