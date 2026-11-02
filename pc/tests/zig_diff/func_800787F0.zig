//! zig_diff spec: func_800787F0 -> func_800787F0.func_800787F0 (146C.c).
//! Inputs: D_800F1868 with bit 1 set or clear (other bits random).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800787F0));

pub const classes = [_][:0]const u8{ "flashing", "released" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const flags = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -3, 0x7FFF_FFFD });
    v.g.D_800F1868().* = flags;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F1868().* & 2 != 0) 0 else 1;
}
