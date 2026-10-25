//! zig_diff spec: func_800A087C -> func_800A087C.func_800A087C (30DB0.c).
//! Inputs: an actorId 0-15 and an arg1 with the edge values (0, 1, 2, the bits
//! it tests, -1, the int extremes) and random bits. Each actor slot 0-9 holds
//! either an actor object (its unk17FD a link to a later slot 0-9 or 0/1, so the
//! recursion is acyclic and in range; 2 comes up often), or a stats object in
//! D_800F45E0, or null. Slots 10-15 hold only stats objects or null. The two
//! wepModels slots share two arena objects, or are null. The C calls only itself,
//! so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A087C));

pub const classes = [_][:0]const u8{ "no-actor-no-scratch", "no-actor-scratch", "actor-no-recursion", "actor-recurses" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actors = v.g.D_800F4538();
    const scratch = v.g.D_800F45E0();
    const models = v.g.vs_battle_wepModels();

    const m0 = ctx.alloc(v.vs_battle_wepModels_t);
    const m1 = ctx.alloc(v.vs_battle_wepModels_t);
    for (models) |*slot| {
        slot.* = switch (ctx.range(u8, 0, 2)) {
            0 => null,
            1 => m0,
            else => m1,
        };
    }

    for (0..16) |i| {
        const ii: u8 = @intCast(i);
        actors[i] = null;
        scratch[i] = null;
        if (i < 10 and ctx.chance(2)) {
            const actor = ctx.alloc(v.D_800F4538_t);
            // Links only point forward (slot > i) and stay in 0..9, so the recursion ends.
            const raw = ctx.range(u8, 0, 9);
            actor.unk17FD = if (raw >= 2 and raw <= ii) 0 else raw;
            actors[i] = actor;
        } else if (ctx.chance(4)) {
            // no stats block for this slot
        } else {
            scratch[i] = ctx.alloc(v.D_800F45E0_t);
        }
    }

    const actorId: c_int = ctx.range(c_int, 0, 15);
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 0x40, 0x800, 0x1000, -1, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{ actorId, arg1 };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[0]);
    const actors = v.g.D_800F4538();
    const actor = actors[id] orelse return if (v.g.D_800F45E0()[id] != null) 1 else 0;
    return if (actor.unk17FD >= 2) 3 else 2;
}
