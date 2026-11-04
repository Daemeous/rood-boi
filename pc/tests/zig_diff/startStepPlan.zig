//! zig_diff spec: func_800E3DDC -> pc.start_step_plan.startStepPlan (D2: pc/src/battle_movement.c).
//! Inputs: a random-filled state block with the fleeing byte (4) zero or not,
//! flags with and without step bits (0xFE0), with bit 16 set or clear, and
//! all-clear low bits (the no-steps test), so the replan and its skip both
//! occur; the callees (func_800E45D4, func_800E4180, func_800E3D5C) are
//! stubs, the last returning any int (it is shifted into bits 18+).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startStepPlan));

pub const classes = [_][:0]const u8{ "keep-plan", "empty-plan", "forced", "fleeing" };

fn dummyTest(state: ?*anyopaque, direction: c_int) callconv(.c) c_int {
    _ = state;
    return direction;
}

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[4] = ctx.edge(u8, &.{ 0, 0, 0, 1, 0x80 });
    var flags: c_uint = ctx.pick(c_uint, &.{ 0, 0xFFFFFFFF, 0x7E0, 0x20, 0x20000, 0x10000, 0xFFFFF01F, 0x1F, 0x10 });
    if (ctx.chance(3)) flags = (flags | 0x20) & ~@as(c_uint, 0x10000); // a plan with steps, not forced
    return .{ block, flags, &dummyTest };
}

pub fn classify(args: Args) usize {
    const flags = args[1];
    if ((flags & 0xFE0) == 0) return 1;
    if ((flags >> 16) & 1 != 0) return 2;
    if (args[0][4] != 0) return 3;
    return 0;
}
