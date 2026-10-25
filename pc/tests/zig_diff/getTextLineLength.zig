//! zig_diff spec: vs_battle_getTextLineLength -> get_text_line_length.getTextLineLength (5BF94.c).
//! Inputs: one byte string in the arena, built token by token so the reader
//! parses it as the generator wrote it: a width byte below 229, a terminator
//! (229, 231 or 232), spacing (250) with a argument byte that covers the full
//! 0-255 range, printDecimal (254) with a digit argument, a 248+ byte that
//! skips one byte, and pass-through bytes (230, 233-247) that consume nothing.
//! Each iteration draws one class (an empty string, or a pool of these
//! tokens), and every string ends in a terminator so the C never runs past
//! what prepare wrote.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getTextLineLength));

pub const classes = [_][:0]const u8{ "empty", "widths", "spacing", "decimal", "chunk-skip", "pass-through", "mixed" };

var cls: usize = 0;

fn token(ctx: *zd.Ctx, class: u8) u8 {
    return switch (class) {
        1 => if (ctx.chance(3)) ctx.edge(u8, &.{ 0, 1, 227, 228 }) else ctx.range(u8, 0, 228),
        2 => 250,
        3 => 254,
        4 => ctx.edge(u8, &.{ 248, 249, 251, 252, 253, 255 }),
        5 => ctx.edge(u8, &.{ 230, 233, 234, 240, 246, 247 }),
        else => switch (ctx.range(u8, 0, 4)) {
            0 => ctx.range(u8, 0, 228),
            1 => 250,
            2 => 254,
            3 => ctx.edge(u8, &.{ 248, 249, 251, 252, 253, 255 }),
            else => ctx.edge(u8, &.{ 230, 233, 234, 240, 246, 247 }),
        },
    };
}

/// The byte after a token that consumes one (the C's `*str++` or `++str`).
fn argument(ctx: *zd.Ctx, tok: u8) u8 {
    if (tok == 254) return ctx.pick(u8, &.{ 0, 8, 9, 10, 11, 19, 20, 99, 100, 255 });
    return ctx.pick(u8, &.{ 0, 1, 127, 128, 129, 200, 255, 9, 10 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(128, 1);
    const class: u8 = if (ctx.chance(10)) 0 else ctx.range(u8, 1, 6);
    cls = class;
    const n: usize = if (class == 0) 0 else ctx.range(usize, 1, 24);
    var i: usize = 0;
    for (0..n) |_| {
        const tok = token(ctx, class);
        buf[i] = tok;
        i += 1;
        if (tok >= 248) {
            buf[i] = argument(ctx, tok);
            i += 1;
        }
    }
    buf[i] = ctx.edge(u8, &.{ 229, 231, 232 });
    return .{buf};
}

pub fn classify(args: Args) usize {
    _ = args;
    return cls;
}
