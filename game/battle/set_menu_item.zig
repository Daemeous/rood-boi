//! Sets up a menu row (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_setMenuItem (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Row `row` of vs_battle_menuItems: shown, at x, y with widths w and backgroundWidth, its gradient state cleared, its text copied (31 bytes at most, the spacing code with its argument, non-printing codes dropped, 0xFF at the end).
pub fn setMenuItem(row: c_int, x: c_int, y: c_int, w: c_int, background_width: c_int, text_arg: [*c]u8) callconv(.c) ?*v.vs_battle_menuItem_t {
    var text = text_arg;
    const item = psx.at(v.g.vs_battle_menuItems().*.?, row);
    item.state = 1;
    item.w = psx.conv(u8, w);
    item.backgroundWidth = psx.conv(u8, background_width);
    c.vs_battle_rMemzero(&item.gradientState, 0x3C);
    item.x = psx.conv(i16, x);
    item.y = psx.conv(i16, y);
    var i: usize = 0;
    while (i < 31) {
        var ch: c_uint = text[0];
        text += 1;
        if (ch == 250) { // vs_char_spacing: copied with the byte after it
            item.text[i] = psx.conv(u8, ch);
            i += 1;
            ch = text[0];
            text += 1;
        } else if (ch == 231) { // vs_char_terminator
            item.text[i] = 0xFF;
            break;
        } else if (ch >= 229) { // vs_char_nonPrinting and above: dropped
            continue;
        }
        item.text[i] = psx.conv(u8, ch); // i <= 31 here: text has 32 bytes
        i += 1;
    }
    return item;
}
