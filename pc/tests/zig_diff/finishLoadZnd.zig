//! zig_diff spec: _finishLoadZnd -> finish_load_znd.finishLoadZnd (146C.c).
//! Inputs: a zone file already queued (an arena slot and header) or not
//! (_loadZnd queues one: the stubs hand out arena blocks); the slot loaded
//! already or after 1..3 frames (vs_main_gametimeUpdate's stub counts
//! them down and marks it loaded); the header's word offsets and its music
//! id 0, the current one or another; D_80050468.unk2 1 or not; the start
//! state 1 or not; the music on (D_800F18B0) or off.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").finishLoadZnd));

pub const classes = [_][:0]const u8{ "queued", "queued-now", "same-music", "new-music" };

/// The frames a slot waits are counted in its last spare byte (arena state:
/// restored for each run, unlike a spec variable).
var frames: u8 = 0;
var music_id: c_int = 0;

fn header(h: *v._zndHeader) void {
    h.enemiesOffset = 0x40;
    h.timOffset = -0x40;
    h.mpdOffset = 0x7FFF_FFF0;
    h.musicId = music_id;
}

pub const stubs = struct {
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*anyopaque {
        _ = file;
        const slot: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
        slot.state = 1;
        @as([*]u8, @ptrCast(slot))[63] = frames;
        return slot;
    }
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        const h = ctx.alloc(v._zndHeader);
        header(h);
        return h;
    }
    pub fn vs_main_getCurrentMusicId(_: *zd.Ctx) c_int {
        return music;
    }
    pub fn vs_main_gametimeUpdate(_: *zd.Ctx, vsync: anytype) c_int {
        _ = vsync;
        const slot = psx.get(v.g._zoneContext().zndCdFile).?;
        const left = &@as([*]u8, @ptrCast(slot))[63];
        if (left.* > 0) left.* -= 1;
        if (left.* == 0) slot.state = 4;
        return 0;
    }
};

/// The zone state _loadZnd reads: slot and data held or not, music on or
/// off, the current music the zone's own or not.
var music: c_int = 0;

fn zone(ctx: *zd.Ctx, id: usize) void {
    const z = v.g._zoneContext();
    if (ctx.chance(2)) z.zndCdFile.raw = 0 else psx.put(&z.zndCdFile, @as(*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(ctx.allocBytes(64, 16)))));
    if (ctx.chance(2)) z.zndData.raw = 0 else psx.put(&z.zndData, @as(*anyopaque, @ptrCast(ctx.allocBytes(16, 16))));
    z.unk30 = if (ctx.chance(3)) 0 else ctx.pick(i32, &.{ 1, -1 });
    const own: c_int = psx.globalMany(u8, "D_8004FCCC")[id];
    music = if (ctx.chance(2)) own else own +% ctx.edge(c_int, &.{ 1, -1, 0x100 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(u8, 0, 255);
    music_id = ctx.edge(c_int, &.{ 0, 1, 7 });
    zone(ctx, id);
    music = if (ctx.chance(2)) music_id else ctx.edge(c_int, &.{ 0, 2 });
    const z = v.g._zoneContext();
    frames = ctx.range(u8, 1, 3);
    if (!ctx.chance(3)) {
        const slot: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
        slot.state = if (ctx.chance(2)) 4 else 3;
        @as([*]u8, @ptrCast(slot))[63] = frames;
        psx.put(&z.zndCdFile, slot);
        const h = ctx.alloc(v._zndHeader);
        header(h);
        psx.put(&z.zndData, @as(*anyopaque, @ptrCast(h)));
    } else {
        z.zndCdFile.raw = 0;
    }
    v.g.D_80050468().unk2 = ctx.edge(u16, &.{ 0, 1, 2 });
    v.g.vs_main_startState().* = ctx.edge(i32, &.{ 0, 1, 2 });
    v.g.D_800F18B0().* = ctx.edge(i32, &.{ 0, 1 });
    return .{@as(c_int, id)};
}

pub fn classify(_: Args) usize {
    const z = v.g._zoneContext();
    if (z.zndCdFile.raw == 0) return 1;
    if (music == music_id) return 2;
    return if (z.unk30 == 0) 0 else 3;
}
