//! zig_diff spec: func_80098194 -> pc.set_room_distance_threshold.setRoomDistanceThreshold (D2: pc/src/room_render.c).
//! Input: one int, a short's value (edges included) or any int, which the C
//! truncates to the file static distance_threshold (globalised, compared).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setRoomDistanceThreshold));

pub const classes = [_][:0]const u8{ "fits", "wraps" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{if (ctx.chance(2)) ctx.pick(c_int, &.{ 0, 8, 0x7ff, 0x7fff, -0x8000, -1 }) else ctx.pick(c_int, &.{ 0x8000, -0x8001, 0x10008, 0x1ffff, -0x80000000, 0x7fffffff })};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] < -0x8000 or args[0] > 0x7fff);
}
