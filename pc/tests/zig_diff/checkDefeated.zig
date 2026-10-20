//! zig_diff spec: func_80074A20 -> actor_defeat.checkDefeated (146C.c).
//! Inputs: an actor id 0-15 (0 is Ashley); every vs_battle_actors slot is
//! empty or an arena actor whose stats (an arena vs_battle_actor2) have 0
//! HP or not, unk26 0 or not, unk27 0x80 or not. D_800F19CC is an arena
//! block (only an address inside it is passed). All four callees are
//! recording stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").checkDefeated));

pub const classes = [_][:0]const u8{ "empty", "alive", "falls-unk26", "defeated", "defeated-0x80", "ashley-0x80" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F19CC().* = ctx.alloc(v.D_800F19CC_t);
    for (0..16) |slot| {
        if (ctx.chance(8)) {
            v.g.vs_battle_actors()[slot] = null;
            continue;
        }
        const actor = ctx.alloc(v.vs_battle_actor);
        const stats = ctx.alloc(v.vs_battle_actor2);
        psx.put(&actor.stats, stats);
        stats.currentHP = if (ctx.chance(2)) 0 else ctx.pick(i16, &.{ 1, -1, 32767 });
        actor.unk26 = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0xFF });
        actor.unk27 = if (ctx.chance(2)) 0x80 else ctx.pick(u8, &.{ 0, 0x7F, 0x81, 0xFF });
        v.g.vs_battle_actors()[slot] = actor;
    }
    return .{if (ctx.chance(4)) 0 else ctx.range(c_int, 0, 15)};
}

pub fn classify(args: Args) usize {
    const actor = v.g.vs_battle_actors()[@intCast(args[0])] orelse return 0;
    if (psx.get(actor.stats).?.currentHP != 0) return 1;
    if (actor.unk26 != 0) return 2;
    if (actor.unk27 != 0x80) return 3;
    return if (args[0] == 0) 5 else 4;
}
