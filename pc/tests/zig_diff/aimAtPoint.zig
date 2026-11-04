//! zig_diff spec: func_800E4FE0 -> aim_at_point.aimAtPoint (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes with the point (0x17C,
//! 0x180) and the position halfwords (0x4C, 0x50) random or an edge, and a
//! kind byte that is any int (truncated into byte 1). ratan2 returns any int;
//! its two arguments (the differences) and func_800E50A0's are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").aimAtPoint));

pub const classes = [_][:0]const u8{"aim"};

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    word(state, 0x17c).* = ctx.pick(u32, &.{ 0, 1, 0xffffffff, 0x7fffffff, 0x80000000, 0x8000, 0xffff, 0x10000 });
    word(state, 0x180).* = ctx.pick(u32, &.{ 0, 1, 0xffffffff, 0x7fffffff, 0x80000000, 0x8000, 0xffff, 0x10000 });
    const kind = ctx.pick(c_int, &.{ 0, 1, 2, 0xff, 0x100, 0x1ff, -1, 0x7FFFFFFF, -0x80000000 });
    return .{ state, kind };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
