//! zig_diff spec: vs_battle_printf -> printf.printf (5BF94.c).
//! Inputs: game text of 0..12 codes then the terminator: plain bytes,
//! two-byte control codes (236..252), hex and decimal codes with an
//! argument (0..9: all digits; 10..255: a fixed count, which wraps past 8
//! hex or 10 decimal digits), string codes (any index: the recursive call
//! is a stub that returns its dest); the context's integers at the edges
//! (0, negative, INT_MIN, small, large). dest is an arena buffer.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").printf));

pub const classes = [_][:0]const u8{ "plain", "numbers", "string" };

var has_number = false;
var has_string = false;

pub const stubs = struct {
    pub fn vs_battle_printf(ctx: *zd.Ctx, dest: anytype, src: anytype) [*c]u8 {
        _ = ctx;
        _ = src;
        return dest;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const context = v.g.stringContext();
    // Not INT_MIN: negated it stays INT_MIN, and a wrapped decimal divisor can reach -1 (INT_MIN / -1 traps in the C).
    for (&context.integers) |*i| {
        i.* = ctx.pick(i32, &.{ 0, 1, -1, 9, 10, 15, 16, 255, 999, 1000, 0x7FFFFFFF, -0x7FFFFFFF, 123456789 });
        if (i.* == -0x80000000) i.* = 0;
    }
    const src = ctx.allocBytes(64, 16);
    const n = ctx.range(usize, 0, 12);
    has_number = false;
    has_string = false;
    var k: usize = 0;
    var i: usize = 0;
    while (i < n) : (i += 1) {
        switch (ctx.range(u8, 0, 5)) {
            0, 1 => {
                src[k] = ctx.range(u8, 0, 235);
                if (src[k] == 231) src[k] = 0;
                k += 1;
            },
            2 => {
                src[k] = ctx.range(u8, 236, 252);
                src[k + 1] = ctx.int(u8);
                k += 2;
            },
            3, 4 => {
                src[k] = ctx.edge(u8, &.{ 253, 254 });
                src[k + 1] = ctx.pick(u8, &.{ 0, 5, 9, 10, 19, 20, 49, 79, 89, 99, 109, 255 });
                has_number = true;
                k += 2;
            },
            else => {
                src[k] = 255;
                src[k + 1] = ctx.pick(u8, &.{ 0, 9 });
                has_string = true;
                k += 2;
            },
        }
    }
    src[k] = 231;
    return .{ ctx.allocBytes(1024, 16), src };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (has_string) return 2;
    return if (has_number) 1 else 0;
}
