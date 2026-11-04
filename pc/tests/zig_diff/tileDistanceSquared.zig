//! zig_diff spec: func_800E45F4 -> pc.tile_distance_squared.tileDistanceSquared (D2: pc/src/battle_movement.c).
//! Inputs: two packed tiles (random bytes 0 and 2, noise in 1 and 3, the
//! extremes 0 and 0xFF). The stub of func_800E4660 records (dx, 0, dz) and
//! returns a drawn value, which must come back unchanged.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tileDistanceSquared));

pub const classes = [_][:0]const u8{ "a-below", "a-not-below" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const edges = [_]c_uint{ 0, 0xFFFFFFFF, 0x00FF00FF, 0xFF00FF00, 0x00010001 };
    return .{ ctx.pick(c_uint, &edges), ctx.pick(c_uint, &edges) };
}

pub fn classify(args: Args) usize {
    return @intFromBool((args[0] & 0xFF) >= (args[1] & 0xFF));
}
