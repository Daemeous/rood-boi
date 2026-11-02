//! zig_diff spec: func_80084B70 -> func_80084B70.func_80084B70 (146C.c).
//! Inputs: source and target entities naming arena actors (slots 0..15;
//! the same actor sometimes) with arena stats; the stat change (the stub's
//! result, which also sets D_800F1A0C or not) and the hp at the 16-bit
//! edges; the target's unk40 0 or not, its actor's unk957 0x80 or not and
//! statuses with and without 0x8000; arg4 0 or not.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084B70));

pub const classes = [_][:0]const u8{ "unrecorded", "recorded", "recorded-worn" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

var amount: i16 = 0;
var critical: c_int = 0;

pub const stubs = struct {
    pub fn _calculateStatChange(_: *zd.Ctx, a: anytype, b: anytype, t: anytype, h: anytype, e: anytype) i16 {
        _ = a;
        _ = b;
        _ = t;
        _ = h;
        _ = e;
        psx.global(c_int, "D_800F1A0C").* = critical;
        return amount;
    }
};

fn entity(ctx: *zd.Ctx) *v._hitEntity_t {
    const e = ctx.alloc(v._hitEntity_t);
    const slot = ctx.range(u8, 0, 15);
    e.unk0.targetActor = slot;
    if (v.g.vs_battle_actors()[slot] == null or ctx.chance(2)) {
        const actor = ctx.alloc(v.vs_battle_actor);
        const stats = ctx.alloc(v.vs_battle_actor2);
        stats.unk957 = if (ctx.chance(3)) 0x80 else ctx.int(u8);
        stats.statuses = ctx.int(i32);
        actor.stats.raw = psx.addr(stats);
        v.g.vs_battle_actors()[slot] = actor;
    }
    return e;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    amount = ctx.pick(i16, &.{ 0, 1, -1, 100, 0x7FFF, -0x8000 });
    critical = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 });
    const source = entity(ctx);
    const target = entity(ctx);
    target.unk0.hp = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    target.unk40 = if (ctx.chance(3)) ctx.pick(i32, &.{ 1, -1 }) else 0;
    const action: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    return .{ action, source, target, ctx.int(c_int), if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 }) };
}

pub fn classify(args: Args) usize {
    if (args[2].?.unk40 != 0) return 0;
    return if (args[4] == 0) 1 else 2;
}
