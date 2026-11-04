//! zig_diff spec: func_800B6500 -> pc.motion_request.setMotionRequestPosition (D2: pc/src/battle_motion.c).
//! Inputs: the actor's slot a random-filled block (0x1900 bytes, the request
//! block at 0x1848 and the position pair at 0x1858/0x185C inside it); a
//! position of two words (edges or any value).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setMotionRequestPosition));

pub const classes = [_][:0]const u8{ "any" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x1900, 16);
    for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const position: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    position[0] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x12345678 });
    position[1] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x12345678 });
    return .{ id, position };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
