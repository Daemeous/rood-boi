//! zig_diff spec: _getPhysicalAttackDamageWithStatusModifiers -> physical_attack_damage.getPhysicalAttackDamageWithStatusModifiers (146C.c).
//! The C is a call of the static _getPhysicalAttackDamage (compiled into
//! the harness with it). Inputs: an arena action (id 0..7, the hit's type,
//! affinity and stat factor random, 0 often); source and target entities
//! naming arena actors 0..3 (the same sometimes) with random stats, small
//! type/class/affinity tables (the indices stay inside), DP/PP at their
//! edges and weapons drawn or not (unk957 names a drawn or sheathed actor);
//! the target limb 0..5; D_800E82F4 random; func_8007FE5C's stub any short;
//! vs_main_getRand's stub 0..99; with or without the variance; hit 0 or 1.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getPhysicalAttackDamageWithStatusModifiers));

pub const classes = [_][:0]const u8{ "plain", "varied" };

pub const stubs = struct {
    pub fn func_8007FE5C(ctx: *zd.Ctx, a: anytype, d: anytype, s: anytype, t: anytype) c_short {
        _ = a;
        _ = s;
        _ = t;
        return if (ctx.value(u1) == 0) d else ctx.edge(c_short, &.{ -1, 0, 1, 0x4000, 0x7FFF, -0x8000 });
    }
    pub fn vs_main_getRand(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.edge(c_int, &.{ 0, 4, 9, 10, 11, 50, 99 });
    }
};

fn wear(ctx: *zd.Ctx, current: *u16, max: *u16) void {
    max.* = ctx.pick(u16, &.{ 0, 1, 100, 0xFFFF });
    current.* = ctx.edge(u16, &.{ 0, max.*, max.* / 2, max.* +% 1 });
}

fn actor(ctx: *zd.Ctx) *v.vs_battle_actor {
    const a = ctx.alloc(v.vs_battle_actor);
    a.weaponDrawn = if (ctx.chance(2)) 1 else 0;
    const s = ctx.alloc(v.vs_battle_actor2);
    s.unk957 = ctx.range(u8, 0, 3);
    s.weapon.damageType = ctx.range(u8, 0, 3);
    s.weapon.blade.category = ctx.range(u8, 0, 9);
    s.weapon.blade.id = if (ctx.chance(3)) 0 else 1;
    s.risk = ctx.pick(u16, &.{ 0, 9, 10, 100, 0xFFFF });
    s.statuses = if (ctx.chance(2)) 2 else 0;
    wear(ctx, &s.weapon.currentDp, &s.weapon.maxDp);
    wear(ctx, &s.weapon.currentPp, &s.weapon.maxPp);
    wear(ctx, &s.shield.currentDp, &s.shield.maxDp);
    wear(ctx, &s.shield.currentPp, &s.shield.maxPp);
    for (&s.limbs) |*l| wear(ctx, &l.armor.currentDp, &l.armor.maxDp);
    a.stats.raw = psx.addr(s);
    return a;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    for (0..4) |k| v.g.vs_battle_actors()[k] = actor(ctx);
    const action = ctx.alloc(v.vs_action_t);
    action.id = ctx.range(u8, 0, 7);
    for (&action.hitParams) |*h| {
        psx.setBits(h, "type", if (ctx.chance(2)) 0 else ctx.range(u2, 1, 3));
        psx.setBits(h, "affinity", if (ctx.chance(2)) 0 else ctx.range(u3, 1, 7));
    }
    const source = ctx.alloc(v._hitEntity_t);
    source.unk0.targetActor = ctx.range(u8, 0, 3);
    const target = ctx.alloc(v._hitEntity_t);
    target.unk0.targetActor = ctx.range(u8, 0, 3);
    target.unk0.targetLimb = ctx.range(i8, 0, 5);
    target.unk40 = if (ctx.chance(2)) 0 else 1;
    return .{ action, source, target, ctx.edge(c_int, &.{ 0, 1 }), if (ctx.chance(2)) 0 else 1 };
}

pub fn classify(args: Args) usize {
    return if (args[4] != 0) 1 else 0;
}
