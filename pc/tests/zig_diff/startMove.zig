//! zig_diff spec: func_800B63AC -> pc.motion_start.startMove (D2: pc/src/battle_motion.c).
//! Inputs: the actor object table D_800F4538 with the actor's slot a random-filled
//! block (0x1900 bytes: byte 0xA, whose bits 6 and 5 pick the path, and byte
//! 0x5B0, whose low two bits the idle path replaces, are inside it); a target
//! pointer to two arena ints; speed and facing (edges or any int). The stubs of
//! func_800A208C and func_800A0204 record their arguments (the model's byte 0xA
//! is already updated when they are called, which the state hash sees).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startMove));

pub const classes = [_][:0]const u8{ "restart", "animated", "idle" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x1900, 16);
    for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    // Byte 0xA: bit 6 set (restart; bit 5 either), bit 5 alone (animated), neither (idle).
    model[0xa] = ctx.edge(u8, &.{ 0x40, 0x60, 0x20, 0x00, 0xFF, 0x9F, 0xBF, 0x5F, 0x1F });
    if (ctx.chance(3)) model[0xa] = ctx.int(u8);
    if (ctx.chance(3)) model[0x5b0] = ctx.edge(u8, &.{ 0x00, 0x01, 0x02, 0x03, 0xFC, 0xFF });
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
    if (model[0xa] & 0x40 != 0) return 0;
    if (model[0xa] & 0x20 != 0) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn func_800A208C(ctx: *zd.Ctx, model: anytype, target: anytype, speed: anytype, facing: anytype) void {
        _ = ctx;
        _ = model;
        _ = target;
        _ = speed;
        _ = facing;
    }

    pub fn func_800A0204(ctx: *zd.Ctx, id: anytype, a: anytype, b: anytype, cc: anytype) c_int {
        _ = id;
        _ = a;
        _ = b;
        _ = cc;
        return ctx.value(c_int);
    }
};
