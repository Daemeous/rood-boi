//! zig_diff spec: func_800D67C4 -> func_800D67C4.func_800D67C4 (5BF94.c).
//! Inputs: any script (unused), frames 0 (made 1), 1 or 255; D_800F55A8 and
//! the keyframes keep the harness's random bytes; func_800D1B18 and
//! func_800D1E20 are stubs (the state hash at each call orders the stores).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D67C4));

pub const classes = [_][:0]const u8{ "zero-frames", "frames" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.alloc(v.D_800F53B8_t), ctx.pick(u8, &.{ 0, 0, 1, 255 }) };
}

pub fn classify(args: Args) usize {
    return if (args[1] == 0) 0 else 1;
}
