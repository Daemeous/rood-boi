//! zig_diff spec: func_80088554 -> func_80088554.func_80088554 (146C.c).
//! Inputs: actors 0..15 present or not (unk27 0x80 sometimes, unk26 0 or
//! not, the weapon drawn or not) with arena stats (some null) at the
//! regeneration edges (HP/MP below, at and above max, 0; risk 0 or not;
//! PP 0 or not; unk954 around 35; statuses with 8 or not); the status stub
//! dying, idle or busy; the reaction and end stubs 0..2; the tick counter
//! D_800E8490 on and off the 30/60/120/180/240 multiples (negative too),
//! the limb 0..5, the tick speed; the event timer's result.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80088554));

pub const classes = [_][:0]const u8{ "off-beat", "beat" };

pub const stubs = struct {
    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return ctx.edge(c_uint, &.{ 0x01000000, 1, 1, 1, 0x2001, 0x1001, 0 });
    }
    pub fn func_8009E4B0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return ctx.edge(c_uint, &.{ 0, 1, 2 });
    }
    pub fn func_800792E4(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        return ctx.edge(c_int, &.{ 0, 0, 1, 2 });
    }
    pub fn func_800BEC58(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_short {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        return ctx.edge(c_short, &.{ 0, 1 });
    }
    pub fn func_800882F4(ctx: *zd.Ctx) c_int {
        return ctx.edge(c_int, &.{ 0, 0, 0, 1 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| {
        v.g.vs_battle_actors()[k] = null;
        if (ctx.chance(3)) continue;
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unk27 = if (ctx.chance(4)) 0x80 else 0;
        actor.unk26 = if (ctx.chance(2)) 0 else 1;
        actor.weaponDrawn = if (ctx.chance(2)) 0 else 1;
        if (ctx.chance(8)) {
            actor.stats.raw = 0;
        } else {
            const stats = ctx.alloc(v.vs_battle_actor2);
            stats.maxHP = 100;
            stats.currentHP = ctx.edge(i16, &.{ 0, 1, 99, 100, 101 });
            stats.maxMP = 50;
            stats.currentMP = ctx.edge(i16, &.{ 0, 49, 50, 51 });
            stats.risk = ctx.edge(u16, &.{ 0, 1, 100 });
            stats.weapon.currentPp = ctx.edge(u16, &.{ 0, 1 });
            stats.shield.currentPp = ctx.edge(u16, &.{ 0, 1 });
            stats.unk954 = ctx.edge(u16, &.{ 0, 1, 35, 36, 1000 });
            stats.statuses = if (ctx.chance(2)) 8 else 0;
            for (&stats.limbs) |*l| {
                l.maxHp = ctx.edge(i16, &.{ 0, 10 });
                l.hp = ctx.edge(i16, &.{ 9, 10, 11 });
            }
            actor.stats.raw = psx.addr(stats);
        }
        v.g.vs_battle_actors()[k] = actor;
    }
    psx.global(c_int, "D_800E8490").* = ctx.edge(c_int, &.{ 0, 30, 60, 90, 120, 180, 240, 360, 480, 600, 710, 719, 15, -30, -1 });
    psx.global(c_int, "D_800E8494").* = ctx.range(c_int, 0, 5);
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 2, 2, 4, 10, 0 });
    return .{};
}

pub fn classify(_: Args) usize {
    return if (@rem(psx.global(c_int, "D_800E8490").*, 30) == 0) 1 else 0;
}
