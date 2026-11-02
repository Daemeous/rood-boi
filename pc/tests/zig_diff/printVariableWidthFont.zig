//! zig_diff spec: _printVariableWidthFont -> print_variable_width_font.printVariableWidthFont (5BF94.c).
//! Inputs: an arena text box with its typewriter count (speed) 0, 1, 2, 5
//! or 100, its font kind 2, 4 or other, its shake unk2C 0, positive or
//! negative, its flags random; its string 0..12 codes then the end (0xE7):
//! printing bytes, page breaks, continue arrows, newlines, the no-op single
//! codes, and the two-byte codes (speed, sound, x / y moves and switches,
//! the shake, hex and decimal numbers and context strings 0..9, and the
//! skipped ones; decimal counts up to 9 digits, see prepare). The context's integers avoid INT_MIN (negated it stays
//! INT_MIN, and a wrapped decimal divisor can reach -1: INT_MIN / -1 traps
//! in the C); its strings are arena texts without page breaks. The OT base
//! D_800F51B8 is an arena block D_800F4E80 indexes; the button and the tick
//! speed random. vs_battle_printVariableWidthFontChar and
//! vs_battle_getTextLineLength are stubs returning any int.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").printVariableWidthFont));

pub const classes = [_][:0]const u8{ "idle", "plain", "codes" };

var has_codes = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const context = v.g.stringContext();
    for (&context.integers) |*i| {
        i.* = ctx.pick(i32, &.{ 0, 1, -1, 9, 10, 15, 16, 255, 999, 1000, 0x7FFFFFFF, -0x7FFFFFFF, 123456789 });
        if (i.* == -0x80000000) i.* = 0;
    }
    for (&context.strings) |*s| {
        const t = ctx.allocBytes(4, 4);
        t[0] = ctx.range(u8, 0, 228);
        t[1] = ctx.edge(u8, &.{ 0xE8, 0x10 });
        t[2] = 0xE7;
        psx.put(s, @as(*u8, @ptrCast(t)));
    }
    v.g.D_800F51B8().* = @ptrCast(@alignCast(ctx.allocBytes(1024 * 4 + 16, 16)));
    const box = ctx.alloc(v.vs_battle_textBox);
    box.speed = ctx.edge(u16, &.{ 0, 1, 2, 5, 100 });
    box.unk2C = ctx.pick(i16, &.{ 0, 0, 1, 8, -1, 0x7FFF });
    psx.setBits(&box.unk0, "unk0_0", ctx.edge(u4, &.{ 2, 4, 0, 1 }));
    v.g.vs_main_buttonsPressed().pad[0].low = ctx.pick(u8, &.{ 0, 0, 1 });
    const text = ctx.allocBytes(40, 16);
    const n = ctx.range(usize, 0, 12);
    has_codes = false;
    var k: usize = 0;
    var i: usize = 0;
    while (i < n) : (i += 1) {
        switch (ctx.range(u8, 0, 7)) {
            0, 1, 2 => {
                text[k] = ctx.range(u8, 0, 228);
                k += 1;
            },
            3 => {
                text[k] = ctx.edge(u8, &.{ 0xE5, 0xE6, 0xE6, 0xE8, 0xE8, 0xE9, 0xEB });
                k += 1;
            },
            4 => {
                text[k] = ctx.edge(u8, &.{ 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFB, 0xFC });
                text[k + 1] = switch (text[k]) {
                    0xF8 => ctx.edge(u8, &.{ 0, 1, 2, 255 }),
                    0xF9 => ctx.edge(u8, &.{ 0, 7 }),
                    0xFA => ctx.edge(u8, &.{ 0, 1, 239, 240, 255 }),
                    0xFB => ctx.edge(u8, &.{ 4, 4, 5, 6, 0, 3, 8, 0xF8 }),
                    else => ctx.int(u8),
                };
                has_codes = true;
                k += 2;
            },
            5 => {
                text[k] = ctx.edge(u8, &.{ 0xFD, 0xFE });
                // A decimal count past 9 digits (110 on) wraps its divisor negative: the digits come out as
                // codes, which the C then runs from its stack buffer. Hex counts wrap to 0 (no digits).
                text[k + 1] = if (text[k] == 0xFE) ctx.edge(u8, &.{ 0, 5, 9, 10, 19, 49, 79, 99, 109 }) else ctx.edge(u8, &.{ 0, 5, 9, 10, 19, 79, 89, 99, 255 });
                has_codes = true;
                k += 2;
            },
            6 => {
                text[k] = 0xFF;
                text[k + 1] = ctx.range(u8, 0, 9);
                has_codes = true;
                k += 2;
            },
            else => {
                text[k] = ctx.edge(u8, &.{ 0xEC, 0xF7 });
                text[k + 1] = ctx.int(u8);
                k += 2;
            },
        }
    }
    text[k] = 0xE7;
    psx.put(&box.string, @as(*u8, @ptrCast(text)));
    return .{box};
}

pub fn classify(args: Args) usize {
    if (args[0].?.speed == 0) return 0;
    return if (has_codes) 2 else 1;
}
