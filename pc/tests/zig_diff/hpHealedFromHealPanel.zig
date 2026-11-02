//! zig_diff spec: _hpHealedFromHealPanel -> hp_healed_from_heal_panel.hpHealedFromHealPanel (146C.c).
//! Inputs: an arena action whose hit (0 or 1) has a random type, affinity
//! (0 too: the C indexes with 255) and stat factor; the target entity names
//! an arena actor (slots 0..15) whose limb (targetLimb -1..6) and shield
//! have DP and PP at their edges (current above max too, max 0) and random
//! factors; its unk957 names an actor whose weapon is drawn or not; the
//! room's trap level any byte; vs_main_getRandSmoothed's stub 0..10.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").hpHealedFromHealPanel));

pub const classes = [_][:0]const u8{ "no-shield", "shield" };

var smoothed: c_int = 0;

pub const stubs = struct {
    pub fn vs_main_getRandSmoothed(_: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return smoothed;
    }
};

fn wear(ctx: *zd.Ctx, current: *u16, max: *u16) void {
    max.* = ctx.pick(u16, &.{ 0, 1, 100, 0xFFFF });
    current.* = ctx.edge(u16, &.{ 0, max.*, max.* / 2, max.* +% 1, 0xFFFF });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    const slot = ctx.range(u8, 0, 15);
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    actor.stats.raw = psx.addr(stats);
    v.g.vs_battle_actors()[slot] = actor;
    const shield_slot = ctx.range(u8, 0, 15);
    if (v.g.vs_battle_actors()[shield_slot] == null) v.g.vs_battle_actors()[shield_slot] = ctx.alloc(v.vs_battle_actor);
    stats.unk957 = shield_slot;
    stats.maxHP = ctx.pick(i16, &.{ 0, 9, 10, 500, 0x7FFF, -1 });
    stats.currentHP = ctx.pick(i16, &.{ 0, 1, 2, 10, 100, -1 });
    stats.risk = ctx.pick(u16, &.{ 0, 1, 100, 0xFFFF });
    const hit = ctx.alloc(v._hitEntity_t);
    hit.unk0.targetActor = slot;
    const limb = ctx.range(i8, -1, 5);
    hit.unk0.targetLimb = limb;
    const armor = &psx.at(&stats.limbs, limb).armor;
    wear(ctx, &armor.currentDp, &armor.maxDp);
    wear(ctx, &stats.shield.currentDp, &stats.shield.maxDp);
    wear(ctx, &stats.shield.currentPp, &stats.shield.maxPp);
    const section = ctx.alloc(v._mpdRoomSectionB);
    psx.put(&v.g.vs_battle_roomData().sectionB, section);
    smoothed = ctx.range(c_int, 0, 10);
    const action = ctx.alloc(v.vs_action_t);
    const nHit = ctx.edge(c_int, &.{ 0, 0, 1 });
    if (ctx.chance(8)) {
        // An amount of exactly 0: no defense (strength 20 and nothing else),
        // a tenth of max HP of -2560 at factor 1 against trap level 255.
        stats.strength = 20;
        stats.accessory.currentStr = 0;
        stats.accessory.types = @splat(0);
        armor.currentStr = 0;
        stats.shield.currentStr = 0;
        stats.maxHP = -25600;
        section.trapLevel = 255;
        psx.setBits(&action.hitParams[@intCast(nHit)], "statFactor", 1);
    }
    return .{ action, ctx.alloc(v._hitEntity_t), hit, nHit, if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 }) };
}

pub fn classify(args: Args) usize {
    const stats = psx.get(v.g.vs_battle_actors()[args[2].?.unk0.targetActor].?.stats).?;
    return if (v.g.vs_battle_actors()[stats.unk957].?.weaponDrawn & 1 != 0) 1 else 0;
}
