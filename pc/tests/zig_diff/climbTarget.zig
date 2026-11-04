//! zig_diff spec: func_800E49D8 -> pc.climb_target.climbTarget (D2: pc/src/battle_movement.c).
//! Inputs: the occupancy bitmap word (scratchpad 0x3F4) zero or not, the
//! direction table at 0x3EC (random bytes; direction 0..15), the actor's
//! tile (0x34) and a target tile (random bytes), and a step count (0, 1, 2, 3,
//! 8, negative, huge). The stubs: func_800E42E0 gives the actor's floor,
//! func_800E4308 the target's raw height (negative one time in five, else
//! the floor + -1..3, with bits above bit 14 that are discarded), and each
//! func_800E42EC the target height -1, 0, +1 or +2 (the floor comparison at
//! its boundary), so the climb ends on the first lower tile, runs out of
//! steps, or is accepted or refused by func_800D96C8 / func_800D954C (which
//! return -1, 0, 1 or 5).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").climbTarget));

pub const classes = [_][:0]const u8{ "no-bitmap", "steps-zero", "steps-negative", "steps-positive" };

var floor: c_int = 0;
var target: c_int = 0;

fn sext15(x: c_int) c_int {
    return (x << 17) >> 17;
}

fn verdict(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u2)) {
        0 => -1,
        1 => 0,
        2 => 1,
        else => 5,
    };
}

pub const stubs = struct {
    pub fn func_800E42E0(ctx: *zd.Ctx, tile: anytype) c_int {
        _ = tile;
        floor = sext15(@as(c_int, ctx.value(u15)));
        return floor;
    }
    pub fn func_800E4308(ctx: *zd.Ctx, tile: anytype) c_int {
        _ = tile;
        const garbage: c_int = @as(c_int, ctx.value(u16)) << 15 & 0x7FFF8000;
        if (ctx.value(u4) == 0) {
            // a raw height of exactly zero: the `raw < 0` test's equal case
            target = 0;
            return 0;
        }
        if (ctx.value(u3) == 0) return @as(c_int, @bitCast(@as(c_uint, ctx.value(u31)) | 0x80000000));
        const deltas = [_]c_int{ -1, 0, 1, 2, 3, 1 };
        target = sext15(floor +% deltas[ctx.value(u8) % deltas.len]);
        return (target & 0x7FFF) | garbage;
    }
    pub fn func_800E42EC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        const deltas = [_]c_int{ -1, 0, 1, 2 };
        return target +% deltas[ctx.value(u2)];
    }
    pub fn func_800D96C8(ctx: *zd.Ctx, state: anytype, from: anytype, to: anytype, dir: anytype) c_int {
        _ = state;
        _ = from;
        _ = to;
        _ = dir;
        return verdict(ctx);
    }
    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, tile: anytype) c_int {
        _ = state;
        _ = tile;
        return verdict(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(4)) @as(*u32, @ptrFromInt(0x1F8003F4)).* = 0 else @as(*u32, @ptrFromInt(0x1F8003F4)).* |= 1;
    return .{
        block,
        ctx.int(c_uint),
        ctx.range(c_int, 0, 15),
        ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 8, -1, 0x7FFFFFFF, -0x80000000 }),
    };
}

pub fn classify(args: Args) usize {
    if (@as(*const u32, @ptrFromInt(0x1F8003F4)).* == 0) return 0;
    if (args[3] == 0) return 1;
    return if (args[3] < 0) 2 else 3;
}
