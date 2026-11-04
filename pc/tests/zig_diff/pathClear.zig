//! zig_diff spec: func_800E44E8 -> pc.navigation_floor.pathClear (D2: pc/src/battle_helpers.c).
//! Inputs: two short XYZ positions in the arena (edges or any i16, so the
//! unsigned read of X and the sign of the steps matter) and two height
//! offsets (edges or any int). The stub of func_800E4478 returns nonzero
//! for every sample (all 16 are drawn, the path is not clear) in some runs,
//! and in the others zero about once in eight calls (the scan returns 1 at
//! the first zero); it records the three coordinates of each sample.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pathClear));

pub const classes = [_][:0]const u8{ "never-stops", "may-stop" };

const edges = [_]i16{ 0, 1, -1, 0x7FFF, -0x8000, 100, -100, 0x1234, -0x1234, 0x4000 };

var may_stop: bool = false;

fn position(ctx: *zd.Ctx) [*]const i16 {
    const p: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    for (0..4) |i| p[i] = ctx.pick(i16, &edges);
    return p;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    may_stop = !ctx.chance(3);
    const from = position(ctx);
    const to = position(ctx);
    return .{
        from,
        to,
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x8000 }),
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x8000 }),
    };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(may_stop);
}

pub const stubs = struct {
    pub fn func_800E4478(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        if (!may_stop) return 1;
        return @intFromBool(ctx.value(u8) & 7 != 0);
    }
};
