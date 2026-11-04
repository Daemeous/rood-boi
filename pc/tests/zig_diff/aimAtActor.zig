//! zig_diff spec: func_800DB74C -> pc.aim_at_actor.aimAtActor (D2: pc/src/battle_movement.c).
//! Inputs: a state block; the model table D_800F4538 with a random-filled
//! model for each of its 17 slots (halfword positions at 0x1C and 0x20, the
//! tile word at 0x5C: random, so negative positions sign-extend); an actor id
//! 0..16. The stub of func_800E2B2C answers 0 or nonzero from the spec; that of
//! func_800DB4AC records the mode.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").aimAtActor));

pub const classes = [_][:0]const u8{ "no-plan", "plan" };

var plan: c_int = 0;

pub const stubs = struct {
    pub fn func_800E2B2C(ctx: *zd.Ctx, state: anytype, id: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = id;
        return plan;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(i16, model[0x1c..][0..2], ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000 }), .little);
        std.mem.writeInt(i16, model[0x20..][0..2], ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000 }), .little);
        table[i] = @ptrCast(model);
    }
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    plan = ctx.edge(c_int, &.{ 0, 0, 1, -1, 2, 0x100 });
    return .{ state, ctx.range(c_int, 0, 16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(plan != 0);
}
