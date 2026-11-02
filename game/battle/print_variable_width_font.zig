//! Prints a text box's string in the variable-width font (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _printVariableWidthFont (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws up to `speed` characters of the box's text (the typewriter), handling page breaks, the continue arrow,
/// the end, newlines, speed, sound, moves, font switches, the shake, and inline numbers and strings; then advances
/// the box's speed, plays the typing sound and steps its shake.
pub fn printVariableWidthFont(box_arg: ?*v.vs_battle_textBox) callconv(.c) void {
    const box = box_arg.?;
    var int_buf: [16]u8 = undefined;
    const ot: [*c]c_uint = @ptrCast(psx.at(v.g.D_800F51B8().*.?, @as(c_int, v.g.D_800F4E80().*) * 4));
    var done: c_int = 0;
    var sfx: c_int = 0;
    var chunks_printed: c_int = 0;
    var saved_string: [*c]u8 = undefined; // set before it is read back
    var speed: c_int = 1;
    var justify: c_int = 0;
    var in_inline_string: c_int = 0;
    var post_continuation: c_int = 0;
    var chars_remaining: c_int = box.speed;
    if (chars_remaining == 0) return;

    v.g._fontTable().* = @intFromBool(psx.getBits(&box.unk0, "unk0_0") != 4);
    v.g._fontBrightness().* = box.brightness;
    v.g.D_800F4CB8().* = psx.conv(u8, (1 - (@as(c_int, @as(*const u8, @ptrCast(box)).*) & 1)) * 4);
    if (psx.getBits(&box.unk0, "unk0_0") == 2) {
        v.g._fontTable().* = 0;
        v.g.D_800F4CB8().* = 0;
    }
    var current_string: [*c]u8 = @ptrCast(psx.get(box.string));
    var print_x: c_int = box.xIndent;
    var print_y: c_int = box.yIndent;
    psx.setBits(&box.unk0, "done", 0);
    while (chars_remaining > 0) {
        if (post_continuation != 0) {
            post_continuation -= 1;
        }
        var current: c_uint = current_string[0];
        current_string += 1;
        if (current < 0xEC) {
            if (current < 229) {
                print_x = c.vs_battle_printVariableWidthFontChar(current, print_x, print_y, ot);
                chars_remaining -%= speed;
                chunks_printed += 1;
                continue;
            }
            switch (current - 0xE5) {
                0 => { // a page break: the box goes on from here
                    psx.put(&box.string, @as(*u8, @ptrCast(current_string)));
                    box.speed = 1;
                    done = 1;
                    box.prevChunksPrinted = 0;
                },
                1 => { // the continue arrow
                    if (chars_remaining == 1) {
                        if (v.g.vs_main_buttonsPressed().pad[0].low != 0) {
                            chars_remaining = 0;
                        } else {
                            done = @rem(@as(c_int, psx.getBits(&box.unk0, "arrowAnimFrame")) +% v.g.vs_gametime_tickspeed().*, 24);
                            print_x = c.vs_battle_printVariableWidthFontChar(@bitCast(0xBC - (done >> 3)), print_x, print_y, ot);
                            psx.setBits(&box.unk0, "arrowAnimFrame", psx.conv(u8, done));
                        }
                        done = 1;
                    } else {
                        chars_remaining -= 1;
                        post_continuation = 2;
                    }
                },
                2 => { // the end, or the end of an inline string
                    if (in_inline_string != 0) {
                        current_string = saved_string;
                        in_inline_string = 0;
                    } else {
                        const shake: u16 = @bitCast(box.unk2C);
                        const shifted: c_int = @as(c_int, shake) << 0x10;
                        if (shifted > 0) {
                            box.unk2C = psx.conv(i16, (shifted >> 0x12) - @as(c_int, shake));
                        }
                        psx.setBits(&box.unk0, "done", 1);
                        if (v.g.vs_main_buttonsPressed().pad[0].low != 0 or post_continuation != 0) {
                            box.speed = 0xFFFF;
                        }
                        done = 1;
                    }
                },
                3 => { // newline
                    print_x = box.xIndent;
                    if (justify != 0) {
                        print_x +%= (@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(current_string)) *% 3;
                    }
                    print_y +%= 13;
                },
                else => {},
            }
            if (done != 0) {
                break;
            }
        } else {
            var param: c_uint = current_string[0];
            current_string += 1;
            switch (current) {
                0xF8 => speed = @bitCast(param),
                0xF9 => sfx = @bitCast(param),
                0xFA => {
                    current = 256;
                    if (param >= 240) {
                        print_x = @bitCast(@as(c_uint, @bitCast(print_x)) -% current +% param);
                    } else {
                        print_x = @bitCast(@as(c_uint, @bitCast(print_x)) +% param);
                    }
                },
                0xFB => {
                    if ((param & 0xF8) != 0) {
                        print_y = @bitCast(@as(c_uint, @bitCast(print_y)) +% (param >> 3));
                    } else if (param == 6) {
                        v.g._fontTable().* = 0;
                    } else if (param == 5) {
                        v.g._fontTable().* = 1;
                    } else if (param == 4) {
                        justify ^= 1;
                        if (justify != 0) {
                            print_x = ((@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(current_string)) *% 3) +% box.xIndent;
                        }
                    } else {
                        v.g.D_800F4CB8().* = (v.g.D_800F4CB8().* & 4) | psx.conv(u8, param);
                    }
                },
                0xFC => {
                    if (box.unk2C > 0) {
                        box.unk2C = psx.conv(i16, -%param);
                    }
                },
                0xFD => { // a hex number from the string context
                    var pos: usize = 0;
                    var value: c_int = v.g.stringContext().integers[param % 10];
                    var divisor: c_int = 0x10000000;
                    if (value < 0) {
                        value = -%value;
                        int_buf[0] = 0xA7;
                        pos = 1;
                    }
                    if (param < 10) {
                        if (value == 0) {
                            int_buf[pos] = 0;
                            pos += 1;
                        } else {
                            while (@divTrunc(value, divisor) == 0) divisor >>= 4;
                            while (divisor != 0) {
                                int_buf[pos] = psx.conv(u8, @divTrunc(value, divisor) & 0xF);
                                pos += 1;
                                divisor >>= 4;
                            }
                        }
                    } else {
                        param = (param / 10) - 1;
                        divisor = 1;
                        while (param != 0) {
                            param -= 1;
                            divisor *%= 16;
                        }
                        while (divisor != 0) {
                            int_buf[pos] = psx.conv(u8, @divTrunc(value, divisor) & 0xF);
                            pos += 1;
                            divisor >>= 4;
                        }
                    }
                    int_buf[pos] = 0xE7;
                    saved_string = current_string;
                    current_string = &int_buf;
                    in_inline_string = 1;
                },
                0xFE => { // a decimal number from the string context
                    var pos: usize = 0;
                    var value: c_int = v.g.stringContext().integers[param % 10];
                    var divisor: c_int = 1000000000;
                    if (value < 0) {
                        value = -%value;
                        int_buf[0] = 0xA7;
                        pos = 1;
                    }
                    if (param < 10) {
                        if (value == 0) {
                            int_buf[pos] = 0;
                            pos += 1;
                        } else {
                            while (@divTrunc(value, divisor) == 0) divisor = @divTrunc(divisor, 10);
                            while (divisor != 0) {
                                int_buf[pos] = psx.conv(u8, @rem(@divTrunc(value, divisor), 10));
                                pos += 1;
                                divisor = @divTrunc(divisor, 10);
                            }
                        }
                    } else {
                        param = (param / 10) - 1;
                        divisor = 1;
                        while (param != 0) {
                            divisor *%= 10;
                            param -= 1;
                        }
                        while (divisor != 0) {
                            int_buf[pos] = psx.conv(u8, @rem(@divTrunc(value, divisor), 10));
                            pos += 1;
                            divisor = @divTrunc(divisor, 10);
                        }
                    }
                    int_buf[pos] = 0xE7;
                    saved_string = current_string;
                    current_string = &int_buf;
                    in_inline_string = 1;
                },
                0xFF => { // an inline string from the string context
                    saved_string = current_string;
                    current_string = @ptrCast(psx.get(psx.at(&v.g.stringContext().strings, param).*)); // strings[10]: the byte is not checked
                    in_inline_string = 1;
                },
                else => {},
            }
        }
    }
    if (chars_remaining <= 0) {
        box.speed +%= psx.conv(u16, v.g.vs_gametime_tickspeed().* >> 1);
    }
    if (box.prevChunksPrinted != chunks_printed and sfx != 0) {
        c.vs_main_playSfxDefault(0x7E, sfx);
    }
    if ((psx.getBits(&box.unk0, "unk0_24") & 0x20) == 0) {
        if (box.unk2C != 0) {
            box.unk2C +%= 1;
        } else {
            _ = c.func_800AAD4C(psx.getBits(&box.unk0, "unk0_24"), 0, 0, 0);
        }
    }
    box.prevChunksPrinted = psx.conv(u16, chunks_printed);
}
