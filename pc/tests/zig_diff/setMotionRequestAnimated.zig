//! zig_diff spec: func_800B653C -> pc.motion_request.setMotionRequestAnimated (D2: pc/src/battle_motion.c).
//! Inputs: the actor's slot a random-filled block (0x1900 bytes: byte 0xA, the
//! request block at 0x1848-0x185F inside it, bit 5 of byte 0xA set or clear);
//! a position of two words; b and c (edges or any int, so only c's low 16
//! bits are stored, over the halfword at 0x184E next to the word at 0x1850).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setMotionRequestAnimated));

pub const classes = [_][:0]const u8{ "bit5-clear", "bit5-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x1900, 16);
    for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(3)) model[0xa] = ctx.edge(u8, &.{ 0x00, 0x20, 0xDF, 0xFF });
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const position: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    position[0] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x12345678 });
    position[1] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x12345678 });
    const b = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 });
    const cc = ctx.pick(c_int, &.{ 0, 1, 0xFFFF, 0x10000, 0x1FFFF, -1 });
    return .{ id, position, b, cc };
}

pub fn classify(args: Args) usize {
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[@intCast(args[0])].?);
    return @intFromBool(model[0xa] & 0x20 != 0);
}
