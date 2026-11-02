//! zig_diff spec: _loadMpd -> load_mpd.loadMpd (146C.c).
//! Inputs: an arena LBA table of 8 files and an id 0..7; the zone's slot and
//! data pointers held or not; the queue-slot and heap stubs hand out arena
//! blocks (the queue stub records the file it was given).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadMpd));

pub const classes = [_][:0]const u8{ "free", "slot-held", "data-held", "both-held" };

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
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const zone = v.g._zoneContext();
    const table: [*]v.vs_main_CdFile = @ptrCast(@alignCast(ctx.allocBytes(8 * @sizeOf(v.vs_main_CdFile), 16)));
    psx.put(&zone.mpdLbas, @as(*v.vs_main_CdFile, &table[0]));
    if (ctx.chance(2)) zone.mpdCdFile.raw = 0 else psx.put(&zone.mpdCdFile, @as(*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(ctx.allocBytes(64, 16)))));
    if (ctx.chance(2)) zone.mpdData.raw = 0 else psx.put(&zone.mpdData, @as(*anyopaque, @ptrCast(ctx.allocBytes(16, 16))));
    return .{ctx.range(c_int, 0, 7)};
}

pub fn classify(_: Args) usize {
    const zone = v.g._zoneContext();
    return @as(usize, @intFromBool(zone.mpdCdFile.raw != 0)) + 2 * @as(usize, @intFromBool(zone.mpdData.raw != 0));
}
