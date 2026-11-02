//! zig_diff spec: func_8007C5C0 -> func_8007C5C0.func_8007C5C0 (146C.c).
//! Inputs: a random arena door section (its zone id any byte: the tables
//! have 256 entries); the zone state _loadZnd reads (held slot and data,
//! music on or off, the current music the zone's or not); the queue-slot
//! and heap stubs hand out arena blocks (the queue stub records the file).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007C5C0));

pub const classes = [_][:0]const u8{ "music-off", "same-music", "other-music" };

pub const stubs = struct {
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*anyopaque {
        const seen: *v.vs_main_CdFile = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.vs_main_CdFile), 16)));
        const f: *const v.vs_main_CdFile = @ptrCast(@alignCast(file));
        seen.* = f.*;
        return @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    }
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        return @ptrCast(ctx.allocBytes(16, 16));
    }
    pub fn vs_main_getCurrentMusicId(_: *zd.Ctx) c_int {
        return music;
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
    const door = ctx.alloc(v._mpdRoomDoorSection_t);
    zone(ctx, door.zndId);
    return .{ door, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const z = v.g._zoneContext();
    if (z.unk30 == 0) return 0;
    return if (music == psx.globalMany(u8, "D_8004FCCC")[args[0].?.zndId]) 1 else 2;
}
