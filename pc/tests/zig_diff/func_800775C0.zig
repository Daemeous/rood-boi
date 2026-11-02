//! zig_diff spec: func_800775C0 -> func_800775C0.func_800775C0 (146C.c).
//! Inputs: slot 0..5 in use or not; arena init data (two-part or not, no
//! second model 0x7F, a blade and shield or none) and spawn entry; the ZUD
//! already loaded (unk14 = index + 1) or another or none (the queue and heap
//! stubs hand out arena blocks; the queue slot is loaded after 0..2 frames,
//! counted in the arena); the ZUD header's sizes 0 or not; the save's room
//! this one or not and a saved state or none; flags' low bits. The data-slot
//! stub records each request but its unk11 before the C first sets it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800775C0));

pub const classes = [_][:0]const u8{ "in-use", "two-part", "armed", "plain" };

var polls: ?*u8 = null;
var queue: ?*u8 = null;

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(size, 16));
    }
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        const zud = ctx.alloc(v.zudHeader);
        zud.effectSize = if (ctx.value(u1) == 0) 0 else 1;
        zud.weaponEffectSize = if (ctx.value(u1) == 0) 0 else 1;
        return zud;
    }
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*anyopaque {
        const seen = ctx.alloc(v.vs_main_CdFile);
        seen.* = @as(*const v.vs_main_CdFile, @ptrCast(@alignCast(file))).*;
        const slot: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
        slot.state = 1;
        return slot;
    }
    pub fn vs_main_gametimeUpdate(_: *zd.Ctx, vsync: anytype) c_int {
        _ = vsync;
        const left = polls.?;
        if (left.* > 0) left.* -= 1;
        if (left.* == 0) {
            if (psx.get(v.g._zoneContext().unk18)) |slot| slot.state = 4;
        }
        return 0;
    }
    pub fn vs_battle_getEmptyObjectDataSlot(_: *zd.Ctx) c_int {
        const left = queue.?;
        if (left.* == 0) return 0;
        left.* -= 1;
        return 1;
    }
    pub fn vs_battle_populateDataSlot(ctx: *zd.Ctx, data: anytype) c_int {
        const request: *const v.vs_battle_objectData = @ptrCast(@alignCast(data.?));
        const seen = ctx.alloc(v.vs_battle_objectData);
        seen.* = request.*;
        if (request.dataType == 2) seen.unk11 = 0;
        if (request.dataType == 8 and request.unk11 != 0xFC and request.unk11 != 0xF0 and request.unk11 != 0xF1) seen.unk11 = 0;
        return 0;
    }
    pub fn func_80069E80(ctx: *zd.Ctx, index: anytype) ?*anyopaque {
        _ = index;
        if (ctx.value(u1) == 0) return null;
        return ctx.alloc(v.savedEnemyState);
    }
    pub fn func_800E6178(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        return ctx.edge(c_int, &.{ 0, 1 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    polls = @ptrCast(ctx.allocBytes(1, 1));
    polls.?.* = ctx.range(u8, 0, 2);
    queue = @ptrCast(ctx.allocBytes(1, 1));
    queue.?.* = ctx.range(u8, 0, 2);
    const zone = v.g._zoneContext();
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    const player = ctx.alloc(v.vs_battle_actor);
    // the actor list's head: none after the player, or another actor
    if (ctx.chance(2)) player.next.raw = 0 else psx.put(&player.next, ctx.alloc(v.vs_battle_actor));
    v.g.vs_battle_actors()[0] = player;
    const id = ctx.range(c_int, 1, 5);
    if (ctx.chance(6)) v.g.vs_battle_actors()[@intCast(id)] = ctx.alloc(v.vs_battle_actor);
    const init = ctx.alloc(v.vs_battle_charInitData);
    init.mpdIdentifer = ctx.range(i32, 0, 3);
    init.unk2 = ctx.edge(u8, &.{ 0, 1, 0x7F });
    for (0..2) |k| psx.globalMany(u8, "D_8004FE88")[k] = @intCast(k);
    psx.globalMany(u8, "D_8004FE88")[0x7F] = 0;
    init.weapon.blade.wepId = if (ctx.chance(3)) 0 else 5;
    init.shield.base.wepId = if (ctx.chance(3)) 0 else 6;
    const files: [*]v.vs_main_CdFile = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.vs_main_CdFile), 16)));
    psx.put(&zone.zudFiles, &files[0]);
    const templates: [*]v.vs_battle_charInitData = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.vs_battle_charInitData), 16)));
    psx.put(&zone.zndEnemies, @as(*v._zndEnemy, @ptrCast(&templates[0])));
    switch (ctx.range(u8, 0, 2)) {
        0 => zone.unk1C.raw = 0,
        1 => {
            psx.put(&zone.unk1C, @as(*anyopaque, @ptrCast(ctx.alloc(v.zudHeader))));
            zone.unk14 = init.mpdIdentifer + 1;
        },
        else => {
            psx.put(&zone.unk1C, @as(*anyopaque, @ptrCast(ctx.alloc(v.zudHeader))));
            zone.unk14 = 99;
        },
    }
    if (ctx.chance(2)) zone.unk18.raw = 0 else psx.put(&zone.unk18, @as(*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(ctx.allocBytes(64, 16)))));
    zone.zndId = 3;
    zone.mapId = 4;
    v.g.D_80060064().* = ctx.range(i32, 0, 3);
    const place = &v.g.D_80061078()[@intCast(v.g.D_80060064().*)];
    place.zndId = if (ctx.chance(2)) 3 else 2;
    place.mapId = if (ctx.chance(4)) 5 else 4;
    const spawn = ctx.alloc(v.mpdEnemySpawn);
    return .{ id, spawn, init, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (v.g.vs_battle_actors()[@intCast(args[0])] != null) return 0;
    const init = args[2].?;
    if (init.unk2 == 1) return 1;
    return if (init.weapon.blade.wepId != 0) 2 else 3;
}
