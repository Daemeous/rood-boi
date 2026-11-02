//! zig_diff spec: func_800841C8 -> func_800841C8.func_800841C8 (146C.c).
//! Inputs: an arena target entity naming an arena actor whose statuses are
//! random, half the time with none of 0xE000 set (or exactly one bit).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800841C8));

pub const classes = [_][:0]const u8{ "none", "some" };

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
    t[1].statuses = ctx.int(i32);
    if (ctx.chance(2)) t[1].statuses &= ~@as(i32, 0xE000);
    if (ctx.chance(4)) t[1].statuses |= ctx.edge(i32, &.{ 0x2000, 0x4000, 0x8000 });
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), t[0], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const e = args[2].?;
    const s = psx.get(v.g.vs_battle_actors()[e.unk0.targetActor].?.stats).?;
    return if (s.statuses & 0xE000 == 0) 0 else 1;
}
