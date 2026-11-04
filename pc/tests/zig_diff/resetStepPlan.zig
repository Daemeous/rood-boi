//! zig_diff spec: func_800E3CDC -> pc.reset_step_plan.resetStepPlan (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose remembered tile (0x194) matches the tile
//! (0x34) in the masked bytes (0 and 2) or not, with the revisit count (0x198)
//! 0, 3, 4 (the fifth repeat), 0xFF or any byte, the actor flag 0x23 zero or
//! not, and random flags. func_800E45D4 is a stub: it returns 0 a quarter of
//! the time (the 0x1F-step case).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").resetStepPlan));

pub const classes = [_][:0]const u8{ "new-tile", "repeat", "repeat-limit" };

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.value(u2);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const tile: *align(1) u32 = @ptrCast(block + 0x34);
    const last: *align(1) u32 = @ptrCast(block + 0x194);
    if (!ctx.chance(3)) last.* = (tile.* & 0xFF00FF) | (ctx.int(u32) & 0xFF00FF00);
    block[0x198] = ctx.edge(u8, &.{ 0, 1, 3, 4, 5, 0xFF, ctx.int(u8) });
    block[0x23] = ctx.edge(u8, &.{ 0, 0, 1, 0x80 });
    return .{ block, ctx.pick(c_uint, &.{ 0, 0xFFFFFFFF, 0x7E0, 0x1F, 0xFFFFF01F, 0x20000 }) };
}

pub fn classify(args: Args) usize {
    const b = args[0];
    const tile = @as(*align(1) const u32, @ptrCast(b + 0x34)).*;
    const last = @as(*align(1) const u32, @ptrCast(b + 0x194)).*;
    if ((last & 0xFF00FF) != (tile & 0xFF00FF)) return 0;
    return if (@as(c_int, b[0x198]) + 1 >= 5) 2 else 1;
}
