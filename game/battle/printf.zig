//! Expands the game text's print codes (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_printf (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Copies game text to `dest` up to the terminator (231, stored), expanding the hex / decimal codes (the context's integers, all digits or a fixed count) and the string code (a context string, recursively); other control codes keep their argument byte. Returns the terminator's place.
pub fn printf(dest_arg: [*c]u8, src_arg: [*c]u8) callconv(.c) [*c]u8 {
    var dest = dest_arg;
    var src = src_arg;
    const context = v.g.stringContext();
    var divisor: c_int = undefined;
    var integer: c_int = undefined;
    var ch: c_int = src[0];
    src += 1;
    while (ch != 231) {
        if (ch >= 236) { // vs_char_control
            switch (ch) {
                253 => { // vs_char_printHex
                    ch = src[0];
                    src += 1;
                    integer = context.integers[@intCast(@rem(ch, 10))];
                    if (integer < 0) {
                        integer = -%integer;
                        dest[0] = 167;
                        dest += 1;
                    }
                    if (ch < 10) {
                        if (integer == 0) {
                            dest[0] = 0;
                            dest += 1;
                        } else {
                            divisor = 0x10000000;
                            while (@divTrunc(integer, divisor) == 0) divisor >>= 4;
                            while (divisor != 0) {
                                dest[0] = psx.conv(u8, @divTrunc(integer, divisor) & 0xF);
                                dest += 1;
                                divisor >>= 4;
                            }
                        }
                    } else {
                        ch = @divTrunc(ch, 10) - 1;
                        divisor = 1;
                        while (ch != 0) {
                            ch -= 1;
                            divisor *%= 16;
                        }
                        while (divisor != 0) {
                            dest[0] = psx.conv(u8, @divTrunc(integer, divisor) & 0xF);
                            dest += 1;
                            divisor >>= 4;
                        }
                    }
                },
                254 => { // vs_char_printDecimal
                    ch = src[0];
                    src += 1;
                    integer = context.integers[@intCast(@rem(ch, 10))];
                    if (integer < 0) {
                        integer = -%integer;
                        dest[0] = 167;
                        dest += 1;
                    }
                    if (ch < 10) {
                        if (integer == 0) {
                            dest[0] = 0;
                            dest += 1;
                        } else {
                            divisor = 1000000000;
                            while (@divTrunc(integer, divisor) == 0) divisor = @divTrunc(divisor, 10);
                            while (divisor != 0) {
                                dest[0] = psx.conv(u8, @rem(@divTrunc(integer, divisor), 10));
                                dest += 1;
                                divisor = @divTrunc(divisor, 10);
                            }
                        }
                    } else {
                        ch = @divTrunc(ch, 10) - 1;
                        divisor = 1;
                        while (ch != 0) : (ch -= 1) {
                            divisor *%= 10;
                        }
                        while (divisor != 0) {
                            dest[0] = psx.conv(u8, @rem(@divTrunc(integer, divisor), 10));
                            dest += 1;
                            divisor = @divTrunc(divisor, 10);
                        }
                    }
                },
                255 => { // vs_char_printString
                    const index = src[0];
                    src += 1;
                    dest = c.vs_battle_printf(dest, @ptrCast(psx.get(psx.at(&context.strings, index).*))); // strings[10]: the byte is not checked
                },
                else => {
                    dest[0] = psx.conv(u8, ch);
                    dest += 1;
                    dest[0] = src[0];
                    dest += 1;
                    src += 1;
                },
            }
        } else {
            dest[0] = psx.conv(u8, ch);
            dest += 1;
        }
        ch = src[0];
        src += 1;
    }
    dest[0] = psx.conv(u8, ch);
    return dest;
}
