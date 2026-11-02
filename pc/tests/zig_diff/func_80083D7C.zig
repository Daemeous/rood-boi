//! zig_diff spec: func_80083D7C -> func_80083D7C.func_80083D7C (146C.c).
//! Inputs: an arena target entity naming an arena actor (random bits
//! around isAnalyzed), arg4 zero or not (both signs).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80083D7C));

pub const classes = [_][:0]const u8{ "no-flag", "flag" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

/// A target entity whose targetActor names an arena actor (slots 0..15:
/// the table's live range) with an arena stats block.
fn targetWithStats(ctx: *zd.Ctx) struct { *v._hitEntity_t, *v.vs_battle_actor2 } {
    const entity = ctx.alloc(v._hitEntity_t);
    const slot = ctx.range(u8, 0, 15);
    entity.unk0.targetActor = slot;
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    actor.stats.raw = psx.addr(stats);
    v.g.vs_battle_actors()[slot] = actor;
    return .{ entity, stats };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = targetWithStats(ctx);
    const flag = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, 2 });
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), t[0], ctx.int(c_int), flag };
}

pub fn classify(args: Args) usize {
    return if (args[4] == 0) 0 else 1;
}
