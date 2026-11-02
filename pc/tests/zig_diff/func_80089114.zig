//! zig_diff spec: func_80089114 -> func_80089114.func_80089114 (146C.c).
//! Inputs: spawning on or off; an arena list of 32 spawn entries (the C reads
//! 32 for the saved states) of which 0..6 count, enemies (type 0) and objects,
//! with each scene/flag/stat test and value at its edges, facings 0..3, slots
//! 0 (any free), 2..11 or 0xFF (objects), ids 0x7F (a plain model) or 0..3 (an
//! arena ZND template, two-part or not by D_8004FE88), the flag stub's value
//! near the tested ones; actors 0..15 present or not (full sometimes); the
//! saved-state stub an arena record or none.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80089114));

pub const classes = [_][:0]const u8{ "off", "on" };

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return ctx.edge(u8, &.{ 0, 1, 2, 3, 4 });
    }
    pub fn func_80069E80(ctx: *zd.Ctx, id: anytype) ?*anyopaque {
        _ = id;
        if (ctx.value(u1) == 0) return null;
        return ctx.alloc(v.savedEnemyState);
    }
    pub fn func_800774FC(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype, e: anytype, f: anytype, flags: anytype, g: anytype) ?*anyopaque {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        _ = e;
        _ = f;
        _ = g;
        const seen = ctx.alloc([4]u8);
        seen.* = @as(*const [4]u8, @ptrCast(flags.?)).*;
        return null;
    }
    pub fn func_800765B0(ctx: *zd.Ctx, a: anytype, b: anytype, flags: anytype, d: anytype, e: anytype) ?*anyopaque {
        _ = a;
        _ = b;
        _ = d;
        _ = e;
        const seen = ctx.alloc([4]u8);
        seen.* = @as(*const [4]u8, @ptrCast(flags.?)).*;
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const zone = v.g._zoneContext();
    psx.global(c_int, "D_800F18E0").* = if (ctx.chance(8)) 0 else 1;
    const spawns: [*]v._mpdEnemy = @ptrCast(@alignCast(ctx.allocBytes(32 * @sizeOf(v._mpdEnemy), 16)));
    for (0..32) |k| {
        const e: *v.mpdEnemySpawn = @ptrCast(&spawns[k]);
        e.@"type" = if (k > 0 and ctx.chance(3)) 1 else 0;
        e.hidden = if (ctx.chance(4)) 1 else 0;
        e.sceneTest = ctx.range(u8, 0, 4);
        e.flagTest = ctx.range(u8, 0, 4);
        e.statTest = ctx.range(u8, 0, 6);
        e.flagValue = ctx.edge(u16, &.{ 1, 2, 3 });
        e.statValue = ctx.edge(u16, &.{ 100, 99, 101, (2 << 10) | 5, (1 << 10) | 5 });
        // entry 0 is an enemy facing 0..3, so param is set before any other facing keeps it
        e.facing = if (k == 0) ctx.range(u8, 0, 3) else ctx.range(u8, 0, 5);
        e.unk24 = if (ctx.chance(2)) 0 else 1;
        e.id = if (ctx.chance(4)) 0x7F else ctx.range(u8, 0, 3);
        e.slot = if (ctx.chance(2)) 0 else ctx.range(u8, 2, 11);
        if (e.@"type" != 0) {
            const o: *v.mpdObjectSpawn = @ptrCast(e);
            o.flagMin = ctx.range(u8, 0, 3);
            o.flagMax = ctx.range(u8, 0, 4);
            o.slot = if (ctx.chance(2)) 0xFF else ctx.range(u8, 2, 15);
        }
    }
    psx.put(&zone.mpdEnemies, &spawns[0]);
    zone.mpdEnemyCount = ctx.range(u32, 0, 6);
    const templates: [*]v._zndEnemy = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v._zndEnemy), 16)));
    for (0..4) |k| templates[k].unk0[2] = ctx.range(u8, 0, 3);
    psx.put(&zone.zndEnemies, &templates[0]);
    for (0..4) |k| psx.globalMany(u8, "D_8004FE88")[k] = if (ctx.chance(2)) 0 else 1;
    const full = ctx.chance(6);
    const last = ctx.chance(6); // only slot 15 free
    for (0..16) |k| v.g.vs_battle_actors()[k] = if (full or last or ctx.chance(4)) ctx.alloc(v.vs_battle_actor) else null;
    if (last and !full) v.g.vs_battle_actors()[15] = null;
    const player = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    stats.currentHP = ctx.edge(i16, &.{ 99, 100, 101 });
    player.stats.raw = psx.addr(stats);
    v.g.vs_battle_characterState().* = player;
    if (ctx.chance(2)) zone.unk1C.raw = 0 else psx.put(&zone.unk1C, @as(*anyopaque, @ptrCast(ctx.allocBytes(16, 16))));
    return .{};
}

pub fn classify(_: Args) usize {
    return if (psx.global(c_int, "D_800F18E0").* == 0) 0 else 1;
}
