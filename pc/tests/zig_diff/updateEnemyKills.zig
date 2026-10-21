//! zig_diff spec: _updateEnemyKills -> score.updateEnemyKills (146C.c).
//! Inputs: an actor in the arena with or without stats, hostile or not,
//! every enemyClass 0-7 (6 and 7 write past enemyKills, as the C does; the
//! odd ones are the B1 alignment case), the streak at the same class or not,
//! its count around 99-101, the streak score near the 999999999 clamp, any
//! maxHP (an i16), subType around 256.
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
const std = @import("std");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").updateEnemyKills));

pub const classes = [_][:0]const u8{ "no-stats", "not-hostile", "new-class", "same-class-under-100", "same-class-at-100", "clamped" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    const score = v.g.scoreData();
    psx.put(&actor.stats, if (ctx.chance(8)) null else stats);
    if (!ctx.chance(6)) actor.allegiance = 4;
    actor.subType = ctx.pick(u16, &.{ 0, 31, 32, 255, 256, 257, 0xFFFF });
    const class = ctx.range(u3, 0, 7);
    psx.setBits(stats, "enemyClass", class);
    stats.maxHP = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 999 });
    if (ctx.chance(2)) score.killStreakEnemyClass = class;
    if (ctx.chance(2)) score.killStreakEnemyCount = ctx.pick(u8, &.{ 98, 99, 100, 101, 255 });
    // Exactly at the clamp sometimes (with maxHP 0 it stays there: the coverage lane's `+25 eq`).
    if (ctx.chance(3)) score.streakScore = if (ctx.chance(4)) 999999999 else 999999999 - ctx.range(u32, 0, 70000);
    if (ctx.chance(4)) score.enemyKills[class % 6] = ctx.pick(u16, &.{ 65534, 65535 });
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const stats = psx.get(actor.stats) orelse return 0;
    if (actor.allegiance != 4) return 1;
    const score = v.g.scoreData();
    if (score.killStreakEnemyClass != psx.getBits(stats, "enemyClass")) return 2;
    if (score.streakScore > 999999999 - 70000) return 5;
    return if (score.killStreakEnemyCount < 100) 3 else 4;
}
