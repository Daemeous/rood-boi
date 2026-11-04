//! zig_diff spec: func_800E4690 -> pc.step_blocked.stepBlocked (D2: pc/src/battle_movement.c).
//! Inputs: a tile (random x/z bytes, noise in 1 and 3) and a direction 0..15
//! indexing the signed direction table at scratchpad 0x3EC (random bytes: dx
//! in bits 0-1, dz in bits 4-5, both signed). The stub of func_800E4320 gives
//! the height of the tile itself, then of the neighbour: a negative raw
//! height one time in four, else the tile's 15-bit height moved by -65..65
//! (the 64 threshold both sides, equal heights, and a positive or negative
//! difference) with random bits above bit 14 that the code discards. Its
//! recorded arguments are the neighbour's coordinates.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").stepBlocked));

pub const classes = [_][:0]const u8{ "dir-low", "dir-high" };

var here: c_int = 0;

fn sext15(x: c_int) c_int {
    return (x << 17) >> 17;
}

pub const stubs = struct {
    pub fn func_800E4320(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        const garbage: c_int = @as(c_int, ctx.value(u16)) << 15 & 0x7FFF8000;
        if (ctx.calls == 0) {
            here = switch (ctx.value(u3)) {
                0 => 0,
                1 => 1,
                2 => -1,
                3 => 64,
                4 => -64,
                else => sext15(@as(c_int, ctx.value(u15))),
            };
            return (here & 0x7FFF) | (if (ctx.value(bool)) garbage else 0);
        }
        if (ctx.value(u2) == 0) return @as(c_int, @bitCast(@as(c_uint, ctx.value(u31)) | 0x80000000));
        const deltas = [_]c_int{ -65, -64, -63, -1, 0, 1, 63, 64, 65, 0 };
        const neighbour = sext15(here +% deltas[ctx.value(u8) % deltas.len]);
        return (neighbour & 0x7FFF) | (if (ctx.value(bool)) garbage else 0);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.pick(c_uint, &.{ 0, 0xFFFFFFFF, 0x00FF00FF, 0x00050005, 0x01000100 }), ctx.range(c_int, 0, 15) };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[1] >= 8);
}
