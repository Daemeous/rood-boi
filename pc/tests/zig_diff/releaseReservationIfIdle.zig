//! zig_diff spec: func_800E6700 -> release_if_idle.releaseReservationIfIdle (D2: pc/src/battle_movement.c).
//! Inputs: an actor id 0..15; its slot of vs_battle_actors empty a fifth of
//! the time, else a block whose command byte 9 is a member of the set
//! 0xA41800 (11, 12, 18, 21, 23, and 43 or 27/28 for the `& 31`), a
//! non-member or any byte; its object in D_800F4538 (always present: a wrong
//! mutation of the membership test reads it for actors the C returns on) with the motion bytes 0xA and 0xB zero, or with one bit
//! outside or inside the tested masks (0x1F, 0xB); its AI entry in D_800F5878
//! empty a third of the time. func_800E678C is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").releaseReservationIfIdle));

pub const classes = [_][:0]const u8{ "no-actor", "wrong-command", "model-busy", "no-entry", "released" };

fn member(command: u8) bool {
    return (@as(u32, 1) << @intCast(command & 31)) & 0xa41800 != 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 15);
    const index: usize = @intCast(id);
    if (ctx.chance(5)) {
        v.g.vs_battle_actors()[index] = null;
    } else {
        const actor = ctx.allocBytes(0x40, 16);
        for (actor[0..0x40]) |*byte| byte.* = ctx.int(u8);
        actor[9] = ctx.pick(u8, &.{ 11, 12, 18, 21, 23, 43, 27, 28, 0, 5, 7 });
        v.g.vs_battle_actors()[index] = @ptrCast(actor);
    }
    {
        const model = ctx.allocBytes(0x40, 16);
        for (model[0..0x40]) |*byte| byte.* = ctx.int(u8);
        model[0xa] = ctx.edge(u8, &.{ 0, 0, 0x10, 0x20, 0xe0, 0x1f, 0x01 });
        model[0xb] = ctx.edge(u8, &.{ 0, 0, 0x08, 0x01, 0x02, 0x04, 0x10, 0xf4 });
        v.g.D_800F4538()[index] = @ptrCast(model);
    }
    if (ctx.chance(3)) {
        v.g.D_800F5878()[index] = null;
    } else {
        const state = ctx.allocBytes(0x40, 16);
        for (state[0..0x40]) |*byte| byte.* = ctx.int(u8);
        v.g.D_800F5878()[index] = @ptrCast(state);
    }
    return .{id};
}

pub fn classify(args: Args) usize {
    const index: usize = @intCast(args[0]);
    const actor: [*]u8 = @ptrCast(v.g.vs_battle_actors()[index] orelse return 0);
    if (!member(actor[9])) return 1;
    const model: [*]u8 = @ptrCast(v.g.D_800F4538()[index].?);
    if ((model[0xa] & 0x1f) | (model[0xb] & 0xb) != 0) return 2;
    return if (v.g.D_800F5878()[index] == null) 3 else 4;
}
