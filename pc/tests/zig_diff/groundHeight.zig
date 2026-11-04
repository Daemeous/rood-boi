//! zig_diff spec: func_800E42EC -> pc.ground_height.groundHeight (D2: pc/src/battle_movement.c).
//! Inputs: tile coordinates (any int). The stub of func_800E4320 returns a
//! drawn value, so bit 14 (the sign of the 15-bit height) and the bits above
//! it, which are discarded, take every combination.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").groundHeight));

pub const classes = [_][:0]const u8{"any"};

pub const stubs = struct {
    pub fn func_800E4320(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        return switch (ctx.value(u3)) {
            0 => 0x3FFF,
            1 => 0x4000,
            2 => 0x7FFF,
            3 => -1,
            4 => 0x8000,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.pick(c_int, &.{ 0, 1, -1, 31 }), ctx.pick(c_int, &.{ 0, 1, -1, 31 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
