//! zig_diff spec: func_800B6404 -> pc.motion_start.restartMove (D2: pc/src/battle_motion.c, through its static motion_reset).
//! Inputs: the actor's slot a random-filled block (byte 0xA random, so the
//! cleared bits 5-6 and the set bit 6 are seen against any prior value); a
//! target pointer to two arena ints; speed and facing (edges or any int). The
//! stub of func_800A208C records its arguments.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").restartMove));

pub const classes = [_][:0]const u8{ "flags-clear", "flags-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x40, 16);
    for (model[0..0x40]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(3)) model[0xa] = ctx.edge(u8, &.{ 0x00, 0x60, 0xFF, 0x9F, 0x20, 0x40 });
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const target: [*]c_int = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    target[0] = ctx.int(c_int);
    target[1] = ctx.int(c_int);
    const speed = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 });
    const facing = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x1000 });
    return .{ id, target, speed, facing };
}

pub fn classify(args: Args) usize {
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[@intCast(args[0])].?);
    return @intFromBool(model[0xa] & 0x60 != 0);
}

pub const stubs = struct {
    pub fn func_800A208C(ctx: *zd.Ctx, model: anytype, target: anytype, speed: anytype, facing: anytype) void {
        _ = ctx;
        _ = model;
        _ = target;
        _ = speed;
        _ = facing;
    }
};
