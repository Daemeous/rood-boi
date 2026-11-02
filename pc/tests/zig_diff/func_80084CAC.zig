//! zig_diff spec: func_80084CAC -> func_80084CAC.func_80084CAC (146C.c).
//! Inputs: an arena target entity naming an arena actor; the stat change
//! (the stub's result) below, at and above the actor's current MP (both
//! signs), and edge mp values so the 16-bit sum wraps.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084CAC));

pub const classes = [_][:0]const u8{ "below", "equal", "capped" };

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
var amount: i16 = 0;

pub const stubs = struct {
    pub fn _calculateStatChange(_: *zd.Ctx, a: anytype, b: anytype, t: anytype, h: anytype, e: anytype) i16 {
        _ = a;
        _ = b;
        _ = t;
        _ = h;
        _ = e;
        return amount;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = targetWithStats(ctx);
    t[1].currentMP = ctx.pick(i16, &.{ 0, 1, -1, 50, 0x7FFF, -0x8000 });
    amount = switch (ctx.range(u8, 0, 2)) {
        0 => t[1].currentMP,
        1 => t[1].currentMP +% ctx.edge(i16, &.{ 1, 2, 100 }),
        else => ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 }),
    };
    t[0].unk0.mp = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    const action: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const source: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    return .{ action, source, t[0], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const e = args[2].?;
    const s = psx.get(v.g.vs_battle_actors()[e.unk0.targetActor].?.stats).?;
    if (s.currentMP < amount) return 2;
    return if (s.currentMP == amount) 1 else 0;
}
