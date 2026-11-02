//! zig_diff spec: _loadMpdRoomSection -> load_mpd_room_section.loadMpdRoomSection (146C.c).
//! Inputs: an arena section whose 24 part lengths are 0, word multiples up
//! to 48 or -4 (all 0 sometimes: the empty section), followed by small
//! part words (0, 1, 4, 8, 60, -1, -4); section E's counts (unk8 positive or not, unk4) and the
//! collision map's size come from those bytes; the saved door 0 or not.
//! memcpy copies (at most 0x400 bytes) as the game's does; the heap stub
//! hands out a block after 64 spare bytes (a -4 length points before it).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadMpdRoomSection));

pub const classes = [_][:0]const u8{ "empty", "no-scene", "scene" };

pub const stubs = struct {
    pub fn vs_main_memcpy(_: *zd.Ctx, dest: anytype, src: anytype, size: anytype) void {
        const n = @min(size, 0x400);
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        @memcpy(d[0..n], s[0..n]);
    }
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        _ = ctx.allocBytes(64, 16);
        return @ptrCast(ctx.allocBytes(0x400, 16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const data: [*]c_int = @ptrCast(@alignCast(ctx.allocBytes(0x60 + 0x400, 16)));
    const empty = ctx.chance(6);
    var total: c_int = 0;
    for (0..24) |k| {
        data[k] = if (empty) 0 else ctx.edge(c_int, &.{ 0, 0, 0, 20, 24, 32, 48, -4 });
        total += data[k];
    }
    if (!empty and total <= 0) data[13] = 24;
    // small part words, so section E's unk4 (a byte count) keeps its unk10 inside the block
    for (0..0x100) |k| data[24 + k] = ctx.edge(c_int, &.{ 0, 1, 4, 8, 60, -1, -4 });
    const flags = v.g.vs_main_stateFlags();
    flags.doorEntered = if (ctx.chance(2)) 0 else ctx.int(u8);
    return .{ ctx.int(c_int), @ptrCast(data) };
}

pub fn classify(args: Args) usize {
    const data: [*]c_int = @ptrCast(@alignCast(args[1].?));
    var total: u32 = 0;
    for (0..24) |k| total +%= @bitCast(data[k]);
    if (total == 0) return 0;
    return if (data[14] > 0) 2 else 1;
}
