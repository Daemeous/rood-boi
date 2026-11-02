//! A text box's whole string drawn at once into a copy of its screen area, or,
//! with the PC's opacity hook, as glyph sprites faded together
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const OpacityOn = *const fn () callconv(.c) c_int;
const OpacityPackets = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// orig: _pcFadeBegin (src/BATTLE/BATTLE.PRG/5BF94.c)
/// 1 when the box's text is drawn as glyph sprites (then nothing is read
/// back): saves the ordering table, the packet heap's position, the colour
/// and the brightness in the C's file statics (globalised by the build:
/// views _pcFadeOt, _pcFadeStart, _pcFadeColor, _pcFadeBrightness) and sets
/// the box's colour and brightness.
fn fadeBegin(box: *v.vs_battle_textBox) bool {
    const on = psx.weak(OpacityOn, "pc_hw_opacity_on") orelse return false;
    if (on() == 0) return false;
    v.g._pcFadeOt().* = @ptrCast(psx.at(v.g.D_800F51B8().*.?, @as(c_int, v.g.D_800F4E80().*) * 4));
    v.g._pcFadeStart().* = psx.get(v.g.vs_scratch().unk0);
    v.g._pcFadeColor().* = v.g.D_800F4CB8().*;
    v.g._pcFadeBrightness().* = v.g._fontBrightness().*;
    v.g.D_800F4CB8().* = psx.conv(u8, (1 - (@as(c_int, @as(*const u8, @ptrCast(box)).*) & 1)) * 4);
    v.g._fontBrightness().* = box.brightness;
    return true;
}

/// orig: _pcFadeGlyph (src/BATTLE/BATTLE.PRG/5BF94.c)
/// One character (its code in the current font table) with its top-left at x, y.
fn fadeGlyph(character: c_uint, x: c_int, y: c_int) void {
    _ = c.vs_battle_printVariableWidthFontChar(character, x, y, @ptrCast(v.g._pcFadeOt().*));
}

/// orig: _pcFadeEnd (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The glyphs drawn since fadeBegin at `alpha` (16.16); the colour and brightness restored.
fn fadeEnd(alpha: c_int) void {
    if (psx.weak(OpacityPackets, "pc_hw_opacity_packets")) |packets| {
        packets(v.g._pcFadeStart().*, psx.get(v.g.vs_scratch().unk0), alpha);
    }
    v.g.D_800F4CB8().* = psx.conv(u8, v.g._pcFadeColor().*);
    v.g._fontBrightness().* = v.g._pcFadeBrightness().*;
}

/// orig: _renderTextImmediate (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws the box's whole string (newlines, the end with its Circle skip,
/// moves, font switches, justification) at `arg1` (16.16 opacity, capped at
/// 1.0): on the PC's glyph path as faded sprites, else into a copy of the
/// box's screen area that is drawn back and freed. A box that does not fit
/// on the 320x224 screen is skipped.
pub fn renderTextImmediate(arg0: ?*v.vs_battle_textBox, arg1_in: c_int) callconv(.c) void {
    const box = arg0.?;
    var arg1 = arg1_in;
    var done: c_int = 0;
    const x: c_int = box.x;
    const y: c_int = box.y;
    var justify: c_int = 0;
    var str: [*]u8 = @ptrCast(psx.get(box.string).?);
    var printX: c_uint = @bitCast(@as(c_int, box.xIndent) -% x);

    const _y = y;
    var printY: c_uint = @bitCast(@as(c_int, box.yIndent) -% y);
    const width: c_int = @as(c_int, box.charsPerLine) * 12 + 10;
    const height: c_int = @as(c_int, box.lineCount) * 13 + 4;

    if (x < 0 or y < 0 or x + width >= 320 or y + height >= 224) return;

    const buf: ?[*]u16 = if (fadeBegin(box)) null else @ptrCast(@alignCast(c.vs_main_allocHeapR(@bitCast(width * height * 2))));

    if ((arg1 >> 0x10) != 0) arg1 = 0x10000;

    var rect: v.RECT = .{
        .x = psx.conv(i16, x +% v.g.vs_main_frameBuf().* *% 320),
        .y = psx.conv(i16, y),
        .w = psx.conv(i16, width),
        .h = psx.conv(i16, height),
    };
    if (buf) |b| _ = c.StoreImage(@ptrCast(&rect), @ptrCast(@alignCast(b)));

    v.g._fontTable().* = 1;

    while (true) {
        var currentChar: c_uint = str[0];
        str += 1;

        if (currentChar < 0xEC) {
            if (currentChar < 229) {
                if (currentChar != 143) {
                    currentChar +%= @as(c_uint, v.g._fontTable().*) * 21 * 9;
                    if (@as(c_uint, @bitCast(width - 12)) >= printX and @as(c_uint, @bitCast(height - 12)) >= printY) {
                        if (buf) |b| {
                            c.func_800C7EBC(@ptrCast(psx.at(b, printX +% printY *% @as(c_uint, @bitCast(width)))), currentChar, width, @bitCast(arg1));
                        } else {
                            fadeGlyph(currentChar -% @as(c_uint, v.g._fontTable().*) * 21 * 9,
                                x +% @as(c_int, @bitCast(printX)), y +% @as(c_int, @bitCast(printY)));
                        }
                    }
                } else if (buf == null) {
                    fadeGlyph(143, x +% @as(c_int, @bitCast(printX)), y +% @as(c_int, @bitCast(printY)));
                }
                printX +%= psx.at(v.g.vs_battle_characterWidths(), currentChar).*;
            } else {
                switch (currentChar -% 229) {
                    2 => {
                        if (v.g.vs_main_buttonsPressed().all & (1 << 5) != 0) {
                            box.speed = 0xFFFF;
                        }
                        done = 1;
                    },
                    3 => {
                        printX = @bitCast(@as(c_int, box.xIndent) -% x);
                        if (justify != 0) {
                            printX +%= @bitCast((@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(str)) *% 3);
                        }
                        printY +%= 13;
                    },
                    else => {},
                }
                if (done != 0) break;
            }
        } else {
            const opcodeParam: c_uint = str[0];
            str += 1;
            switch (currentChar -% 0xF8) {
                2 => {
                    currentChar = 256;
                    if (opcodeParam >= 0xF0) {
                        printX = printX -% currentChar +% opcodeParam;
                    } else {
                        printX +%= opcodeParam;
                    }
                },
                3 => {
                    if (opcodeParam & 0xF8 != 0) {
                        printY +%= opcodeParam >> 3;
                    } else if (opcodeParam == 6) {
                        v.g._fontTable().* = 0;
                    } else if (opcodeParam == 5) {
                        v.g._fontTable().* = 1;
                    } else if (opcodeParam == 4) {
                        justify ^= 1;
                        if (justify != 0) {
                            printX = @bitCast(((@as(c_int, box.charsPerLine) * 2 -% c.vs_battle_getTextLineLength(str)) *% 3 +%
                                @as(c_int, box.xIndent)) -% x);
                        }
                    }
                },
                else => {},
            }
        }
    }

    const b = buf orelse {
        fadeEnd(arg1);
        return;
    };
    c.vs_battle_renderImage((x +% v.g.vs_main_frameBuf().* *% 320) | (_y << 0x10), @ptrCast(b), width | (height << 0x10));
    _ = c.DrawSync(0);
    c.vs_main_freeHeapR(@ptrCast(b));
}
