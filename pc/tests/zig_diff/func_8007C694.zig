//! zig_diff spec: func_8007C694 -> func_8007C694.func_8007C694 (146C.c).
//! Inputs: a location 0..63 whose zone and map ids match D_800F1AB0's or
//! not (both, one, neither); arg1 and arg2 past their 5 bits; actor 0's
//! facing (func_800A1108's stub writes it) at the turn and quarter edges,
//! negative too; the zone state _loadZnd reads.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007C694));

pub const classes = [_][:0]const u8{ "same-place", "new-map", "new-zone" };

var facing: i16 = 0;

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
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.pad = facing;
        return ctx.value(c_int);
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
    const location = ctx.range(u8, 0, 63);
    const ids = v.g.vs_main_locationZoneMapIds();
    const door = v.g.D_800F1AB0();
    const zndId = ids[2 * @as(usize, location)];
    const mpdId = ids[2 * @as(usize, location) + 1];
    switch (ctx.range(u8, 0, 3)) {
        0 => {
            door.zndId = zndId;
            door.mpdId = mpdId;
        },
        1 => {
            door.zndId = zndId;
            door.mpdId = mpdId +% 1;
        },
        else => door.zndId = zndId +% ctx.edge(u8, &.{ 1, 0x80 }),
    }
    zone(ctx, zndId);
    facing = ctx.pick(i16, &.{ 0, 0x1FF, 0x200, 0x5FF, 0x600, 0xDFF, 0xE00, 0xFFF, -0x200, -0x201, -0x600, -0x601, -1, 0x7FFF, -0x8000 });
    return .{ location, ctx.pick(c_int, &.{ 0, 31, 32, -1 }), ctx.pick(c_int, &.{ 0, 31, 32, -1 }), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const ids = v.g.vs_main_locationZoneMapIds();
    const door = v.g.D_800F1AB0();
    const at: usize = @intCast(args[0] * 2);
    if (ids[at] != door.zndId) return 2;
    return if (ids[at + 1] != door.mpdId) 1 else 0;
}
