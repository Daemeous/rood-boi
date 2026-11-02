//! zig_diff spec: vs_battle_renderImage -> render_image.renderImage (5BF94.c).
//! Inputs: the RECT cursor D_800EC280 0..7 (7 wraps to 0), xy and wh any
//! words, the buffer an arena block; LoadImage is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderImage));

pub const classes = [_][:0]const u8{ "first", "wraps", "middle" };

pub fn prepare(ctx: *zd.Ctx) Args {
    psx.global(c_int, "D_800EC280").* = ctx.range(c_int, 0, 7);
    return .{ ctx.int(c_int), @as(*anyopaque, @ptrCast(ctx.allocBytes(64, 16))), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const cursor = psx.global(c_int, "D_800EC280").*;
    if (cursor == 0) return 0;
    return if (cursor == 7) 1 else 2;
}
