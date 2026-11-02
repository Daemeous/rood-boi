//! Prints a text box's string in the fixed-width font (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _printFixedWidthFont (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws the box's text at `scale` (12 per character, 13 per line) through _printFixedWidthFontChar, handling
/// the terminator, newlines (justified when on), x moves, y moves and the font, justify and colour codes.
pub fn printFixedWidthFont(box_arg: ?*v.vs_battle_textBox, scale: c_int) callconv(.c) void {
    const box = box_arg.?;
    var justify: c_int = 0;
    var str: [*c]u8 = @ptrCast(psx.get(box.string));
    const char_width: c_int = scale *% 12;
    const char_height: c_int = char_width;
    const line_height: c_int = scale *% 13;
    if (box.speed != 0) {
        var print_x: c_int = (@as(c_int, box.unk24) << 16) +% ((@as(c_int, box.xIndent) - box.x) *% scale);
        var print_y: c_int = (@as(c_int, box.unk26) << 16) +% ((@as(c_int, box.yIndent) - box.y) *% scale);
        v.g.D_800F4CB8().* = psx.conv(u8, (1 - (@as(c_int, @as(*const u8, @ptrCast(box)).*) & 1)) * 4);
        v.g._fontTable().* = @intFromBool(psx.getBits(&box.unk0, "unk0_0") != 4);
        v.g._fontBrightness().* = box.brightness;
        const line_start = print_x;
        if (psx.getBits(&box.unk0, "unk0_0") == 2) {
            v.g._fontTable().* = 0;
            v.g.D_800F4CB8().* = 0;
        }
        var done: c_int = box.brightness;
        v.g._fontBrightness().* = done | (done << 8) | (done << 0x10) | 0x2C000000;
        done = 0;
        while (true) {
            const current: c_uint = str[0];
            str += 1;
            if (current < 0xEC) {
                if (current < 229) {
                    print_x = c._printFixedWidthFontChar(@bitCast(current), print_x, print_y, char_width, char_height, scale);
                    continue;
                }
                switch (current - 229) {
                    2 => done = 1, // vs_char_terminator
                    3 => { // vs_char_newline
                        print_x = line_start;
                        if (justify != 0) {
                            print_x = line_start +% ((@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(str)) *% (char_width >> 2));
                        }
                        print_y +%= line_height;
                    },
                    else => {},
                }
                if (done != 0) {
                    break;
                }
            } else {
                const param: c_uint = str[0];
                str += 1;
                switch (current -% 0xF8) {
                    2 => {
                        // Unsigned, as the C's u_int parameter makes it.
                        if (param >= 0xF0) {
                            print_x = @bitCast(@as(c_uint, @bitCast(print_x)) -% ((256 - param) *% @as(c_uint, @bitCast(char_width))) / 12);
                        } else {
                            print_x = @bitCast(@as(c_uint, @bitCast(print_x)) +% (param *% @as(c_uint, @bitCast(char_width))) / 12);
                        }
                    },
                    3 => {
                        if ((param & 0xF8) != 0) {
                            print_y = @bitCast(@as(c_uint, @bitCast(print_y)) +% (param >> 3) *% @as(c_uint, @bitCast(scale)));
                        } else if (param == 6) {
                            v.g._fontTable().* = 0;
                        } else if (param == 5) {
                            v.g._fontTable().* = 1;
                        } else if (param == 4) {
                            justify ^= 1;
                            if (justify != 0) {
                                print_x = line_start +% ((@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(str)) *% (char_width >> 2));
                            }
                        } else {
                            v.g.D_800F4CB8().* = (v.g.D_800F4CB8().* & 4) | psx.conv(u8, param);
                        }
                    },
                    else => {},
                }
            }
        }
    }
}
