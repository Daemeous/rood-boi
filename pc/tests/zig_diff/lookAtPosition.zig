//! zig_diff spec: func_800B65E8 -> pc.look_request.lookAtPosition (D2: pc/src/battle_motion.c, through its static look_request).
//! Inputs: the actor object table D_800F4538 with the actor's slot a random-filled
//! block (0x700 bytes: the look target's words, the halfword over the second
//! word's high half and the kind byte at 0xB are all inside it); a target
//! position of two ints (edges or any value) in the arena; a halfword `b` (edges,
//! any int, so only its low 16 bits are stored); and byte 0xB with the bits the
//! store keeps (4-6 are replaced, the rest preserved) set or clear.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lookAtPosition));

pub const classes = [_][:0]const u8{ "kept-bits-clear", "kept-bits-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x700, 16);
    for (model[0..0x700]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const position: [*]c_int = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    position[0] = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x12345678 });
    position[1] = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x12345678 });
    // Byte 0xB: random, or with the replaced bits (4-6) set alone, or fully clear.
    if (ctx.chance(3)) model[0xb] = ctx.edge(u8, &.{ 0, 0x70, 0x0F, 0x80 });
    const b = ctx.pick(c_int, &.{ 0, 1, 0xFFFF, 0x10000, 0x1FFFF, -1 });
    return .{ id, position, b };
}

pub fn classify(args: Args) usize {
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[@intCast(args[0])].?);
    return @intFromBool(model[0xb] & 0x8f != 0);
}
