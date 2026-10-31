//! zig_diff spec: vs_battle_getActionFlags -> get_action_flags.getActionFlags (146C.c).
//! Inputs: actor 0 (a player, one in three) with its action unlocked or not, or
//! any other actor (1-15). The action index sits on the 0, 140-142, 223-224 and
//! 255 edges half the time and is otherwise in 0-255, so vs_main_actions is always
//! read in bounds. The actor's stats carry statuses with the 0x1000 and 0x2000
//! bits, with the sign bit on and off, unk954 zero or not, and weaponDrawn zero or
//! not. _getActionCost returns zero, one bit, the 0x00FFFFFF boundary (exactly
//! equal to the mask's complement often enough to exercise the `& 0xFF000000`
//! boundary), or any word, and may rewrite the stats' statuses, which the C reads
//! back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
const psx = @import("common").psx;

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getActionFlags));

pub const classes = [_][:0]const u8{ "other-low", "other-mid", "other-high", "player-locked", "player-unlocked" };

pub const stubs = struct {
    pub fn _getActionCost(ctx: *zd.Ctx, id: anytype, actor: anytype, flag: anytype) c_int {
        _ = id;
        _ = flag;
        const s: *v.vs_battle_actor2 = @ptrCast(@alignCast(actor.?));
        if (ctx.value(u1) == 1) s.statuses = ctx.value(i32);
        const r: u32 = switch (ctx.value(u2)) {
            0 => 0,
            1 => @as(u32, 1) << ctx.value(u5),
            2 => ctx.edge(u32, &.{ 0x00FF_FFFF, 0x00FF_FFFF, 0x0100_0000, 0xFF00_0000 }),
            else => ctx.value(u32),
        };
        return @bitCast(r);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    const actorId: c_int = if (ctx.chance(3)) 0 else ctx.range(c_int, 1, 15);
    v.g.vs_battle_actors()[@intCast(actorId)] = actor;
    psx.put(&actor.stats, stats);
    actor.weaponDrawn = ctx.pick(i32, &.{ 0, 1, -1, -2147483648, 2147483647 });
    stats.statuses = ctx.pick(i32, &.{ 0, 0x1000, 0x2000, 0x3000, -4096, -8192, -2147483648, 0x4000 });
    stats.unk954 = ctx.pick(u16, &.{ 0, 1, 2, 0x8000, 0xFFFF });
    const actionId: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 140, 141, 142, 223, 224, 255 })
    else
        ctx.range(c_int, 0, 255);
    if (actorId == 0) psx.setBits(&v.g.actions()[@intCast(actionId)], "unlocked", ctx.int(u1));
    return .{ actorId, actionId };
}

pub fn classify(args: Args) usize {
    const actorId = args[0];
    const actionId = args[1];
    if (actorId == 0) {
        const action = &v.g.actions()[@intCast(actionId)];
        return if (psx.getBits(action, "unlocked") == 0) 3 else 4;
    }
    if (actionId < 141) return 0;
    if (actionId < 224) return 1;
    return 2;
}
