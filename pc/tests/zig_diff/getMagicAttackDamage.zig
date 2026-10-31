//! zig_diff spec: _getMagicAttackDamage -> get_magic_attack_damage.getMagicAttackDamage (146C.c).
//! Inputs: an action with two hit params (affinity 0 or explicit, type 0-3,
//! statFactor 0-31), a source and a target hit entity whose actors come from
//! a 16-slot battle actor table (every slot valid, weaponDrawn with and without
//! bit 0 and the sign bit, unk957 indices 0-15), four random actor2 blocks
//! whose weapon, accessory, limb, armor and shield numbers are biased to zero,
//! negative and extreme values (so maxDp/maxPp are zero about a third of the
//! time, affinity sums tie at zero often, and products overflow), nHit at 0,
//! 1 and the out-of-list 2 and -1 (in the action's own bytes), and addRandMod
//! at 0, 1, 2 and -1. The stubs return 0, -1 or any value for the damage
//! function, and write the fields the real callees may change.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getMagicAttackDamage));

pub const classes = [_][:0]const u8{ "stray-hit-index", "explicit-affinity", "auto-unarmed", "auto-armed" };

fn edgeI16(ctx: *zd.Ctx) i16 {
    return ctx.pick(i16, &.{ 0, 0, 1, -1, 100, 32767, -32768 });
}

fn fillI16(ctx: *zd.Ctx, xs: []i16) void {
    for (xs) |*x| x.* = edgeI16(ctx);
}

fn fillStats(ctx: *zd.Ctx, s: *v.vs_battle_actor2) void {
    s.unk957 = ctx.range(u8, 0, 15);
    s.intelligence = ctx.pick(u16, &.{ 0, 1, 100, 32767, 65535 });
    s.risk = ctx.pick(u16, &.{ 0, 2, 65534, 65535 });
    s.weapon.blade.id = ctx.pick(u16, &.{ 0, 0, 1, 7 });
    s.weapon.currentInt = edgeI16(ctx);
    s.weapon.maxDp = ctx.pick(u16, &.{ 0, 1, 100, 65535 });
    s.weapon.currentDp = ctx.pick(u16, &.{ 0, 1, 50, 65535 });
    s.weapon.maxPp = ctx.pick(u16, &.{ 0, 1, 100, 65535 });
    s.weapon.currentPp = ctx.pick(u16, &.{ 0, 1, 50, 65535 });
    fillI16(ctx, &s.weapon.classAffinityCurrent.affinity[0]);

    s.accessory.currentInt = edgeI16(ctx);
    for (&s.accessory.types) |*x| x.* = ctx.pick(u16, &.{ 0, 1, 32768, 65535 });
    for (&s.accessory.affinities) |*x| x.* = ctx.pick(u16, &.{ 0, 1, 32768, 65535 });

    for (&s.limbs) |*l| {
        fillI16(ctx, &l.affinities);
        fillI16(ctx, &l.types);
        l.armor.currentInt = edgeI16(ctx);
        l.armor.maxDp = ctx.pick(u16, &.{ 0, 1, 100, 65535 });
        l.armor.currentDp = ctx.pick(u16, &.{ 0, 1, 50, 65535 });
        for (&l.armor.types) |*x| x.* = ctx.pick(u16, &.{ 0, 1, 32768, 65535 });
        fillI16(ctx, &l.armor.classAffinityCurrent.affinity[0]);
    }

    s.shield.currentInt = edgeI16(ctx);
    s.shield.maxDp = ctx.pick(u16, &.{ 0, 1, 100, 65535 });
    s.shield.currentDp = ctx.pick(u16, &.{ 0, 1, 50, 65535 });
    s.shield.maxPp = ctx.pick(u16, &.{ 0, 1, 100, 65535 });
    s.shield.currentPp = ctx.pick(u16, &.{ 0, 1, 50, 65535 });
    for (&s.shield.types) |*x| x.* = ctx.pick(u8, &.{ 0, 1, 100, 255 });
    fillI16(ctx, &s.shield.classAffinityCurrent.affinity[0]);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.vs_battle_actors();

    var blocks: [4]*v.vs_battle_actor2 = undefined;
    for (&blocks) |*b| {
        b.* = ctx.alloc(v.vs_battle_actor2);
        fillStats(ctx, b.*);
    }
    for (0..16) |i| {
        const actor = ctx.alloc(v.vs_battle_actor);
        psx.put(&actor.stats, blocks[i % 4]);
        actor.weaponDrawn = ctx.pick(i32, &.{ 0, 1, 3, -1, 0x7FFF_FFFE, 2 });
        table[i] = actor;
    }

    const action = ctx.alloc(v.vs_action_t);
    for (0..2) |i| {
        const hp = &action.hitParams[i];
        const aff: u3 = if (ctx.chance(2)) 0 else ctx.range(u3, 1, 7);
        psx.setBits(hp, "affinity", aff);
        psx.setBits(hp, "type", ctx.range(u2, 0, 3));
        psx.setBits(hp, "statFactor", ctx.range(u5, 0, 31));
    }

    const source = ctx.alloc(v._hitEntity_t);
    const target = ctx.alloc(v._hitEntity_t);
    source.unk0.targetActor = ctx.range(u8, 0, 15);
    target.unk0.targetActor = ctx.range(u8, 0, 15);
    target.unk0.targetLimb = ctx.range(i8, 0, 5);
    target.unk40 = ctx.pick(i32, &.{ 0, 0, 1, -1 });

    const nHit = ctx.edge(c_int, &.{ 0, 0, 1, -1, 2 });
    const addRandMod = ctx.edge(c_int, &.{ 0, 1, 2, -1 });
    return .{ action, source, target, nHit, addRandMod };
}

pub fn classify(args: Args) usize {
    const action = args[0].?;
    const source = args[1].?;
    const nHit = args[3];
    if (nHit != 0 and nHit != 1) return 0;
    const hp = &action.hitParams[@intCast(nHit)];
    if (psx.getBits(hp, "affinity") != 0) return 1;
    const table = v.g.vs_battle_actors();
    const src = psx.get(table[source.unk0.targetActor].?.stats).?;
    const drawn = (table[src.unk957].?.weaponDrawn & 1) != 0;
    if (!drawn or src.weapon.blade.id == 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_8007FE5C(ctx: *zd.Ctx, action: anytype, damage: anytype, source: anytype, target: anytype) c_short {
        _ = action;
        _ = damage;
        _ = source;
        const t: ?*v.vs_battle_actor2 = @ptrCast(@alignCast(target));
        if (t) |p| {
            if (ctx.value(u1) == 1) p.currentHP = ctx.value(i16);
        }
        const r = ctx.value(u8);
        if (r < 80) return 0;
        if (r < 120) return -1;
        return ctx.value(c_short);
    }

    pub fn func_80080C9C(ctx: *zd.Ctx, action: anytype, target: anytype, a: anytype, b: anytype, limb: anytype) void {
        _ = action;
        _ = a;
        _ = b;
        _ = limb;
        const t: ?*v.vs_battle_actor2 = @ptrCast(@alignCast(target));
        if (t) |p| {
            if (ctx.value(u1) == 1) p.currentMP = ctx.value(i16);
        }
    }

    pub fn vs_main_getRand(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return @intCast(ctx.value(u8) % 5);
    }
};
