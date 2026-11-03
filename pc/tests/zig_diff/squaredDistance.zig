//! zig_diff spec: func_800E4624 -> pc.squared_distance.squaredDistance (D2: pc/src/battle_helpers.c).
//! Inputs: two XYZ short positions (edges or any i16, so differences leave
//! the i16 range), and two height offsets (0, small, or any int, so the sum
//! with Y wraps). The stub of func_800E4660 records the three differences it
//! is handed and returns a drawn value, which must come back unchanged.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").squaredDistance));

pub const classes = [_][:0]const u8{ "no-heights", "heights" };

const edges = [_]i16{ 0, 1, -1, 0x7FFF, -0x8000, 100, -100 };

fn position(ctx: *zd.Ctx) [*]const i16 {
    const p: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    for (0..4) |i| p[i] = ctx.pick(i16, &edges);
    return p;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = position(ctx);
    const b = position(ctx);
    const none = ctx.chance(3);
    return .{
        a,
        b,
        if (none) 0 else ctx.pick(c_int, &.{ 1, -1, 0x7FFFFFFF, -0x80000000, 0x8000 }),
        if (none) 0 else ctx.pick(c_int, &.{ 1, -1, 0x7FFFFFFF, -0x80000000, 0x8000 }),
    };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[2] != 0 or args[3] != 0);
}

pub const stubs = struct {
    pub fn func_800E4660(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        return ctx.value(c_int);
    }
};
