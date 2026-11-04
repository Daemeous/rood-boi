//! zig_diff spec: func_800DB5FC -> pc.start_patrol.startPatrol (D2: pc/src/battle_movement.c).
//! Inputs: a state block. The stubs of func_800DB4AC (mode 5) and func_800E23AC
//! record their calls, so the mode and the order of the two calls show.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startPatrol));

pub const classes = [_][:0]const u8{"patrol"};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x100, 16);
    for (block[0..0x100]) |*byte| byte.* = ctx.int(u8);
    return .{block};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
