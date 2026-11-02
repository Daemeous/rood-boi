//! zig_diff spec: _printFixedWidthFont -> print_fixed_width_font.printFixedWidthFont (5BF94.c).
//! Inputs: an arena text box (speed 0: nothing drawn), its font kind 2, 4
//! or other, its string 0..10 codes then the terminator: printing bytes,
//! newlines (232), the other single codes 229..235, two-byte codes from
//! 0xEC (x moves 0xFA with a parameter below or from 0xF0, y moves and
//! font / justify / colour switches 0xFB, the rest skipped); scale 1, 2,
//! 0 or -1 (the moves divide unsigned). _printFixedWidthFontChar and
//! vs_battle_getTextLineLength are stubs returning any int.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").printFixedWidthFont));

pub const classes = [_][:0]const u8{ "idle", "plain", "codes" };

var has_codes = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const box = ctx.alloc(v.vs_battle_textBox);
    box.speed = ctx.pick(u16, &.{ 0, 1, 1, 1 });
    psx.setBits(&box.unk0, "unk0_0", ctx.edge(u4, &.{ 2, 4, 0, 1 }));
    const text = ctx.allocBytes(32, 16);
    const n = ctx.range(usize, 0, 10);
    has_codes = false;
    var k: usize = 0;
    var i: usize = 0;
    while (i < n) : (i += 1) {
        switch (ctx.range(u8, 0, 5)) {
            0, 1 => {
                text[k] = ctx.range(u8, 0, 228);
                k += 1;
            },
            2 => {
                text[k] = ctx.edge(u8, &.{ 232, 232, 229, 230, 233, 235 });
                k += 1;
            },
            3 => {
                text[k] = 0xFA;
                text[k + 1] = ctx.edge(u8, &.{ 0, 1, 0xEF, 0xF0, 0xFF });
                has_codes = true;
                k += 2;
            },
            4 => {
                text[k] = 0xFB;
                text[k + 1] = ctx.edge(u8, &.{ 4, 4, 5, 6, 0, 3, 7, 8, 0xF8 });
                has_codes = true;
                k += 2;
            },
            else => {
                text[k] = ctx.edge(u8, &.{ 0xEC, 0xF9, 0xFC, 0xFF });
                text[k + 1] = ctx.int(u8);
                k += 2;
            },
        }
    }
    text[k] = 231;
    psx.put(&box.string, @as(*u8, @ptrCast(text)));
    return .{ box, ctx.pick(c_int, &.{ 1, 2, 0, -1 }) };
}

pub fn classify(args: Args) usize {
    if (args[0].?.speed == 0) return 0;
    return if (has_codes) 2 else 1;
}
