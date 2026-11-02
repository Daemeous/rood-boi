//! zig_diff spec: func_8008E4DC -> func_8008E4DC.func_8008E4DC (146C.c).
//! Inputs: D_800F1D6F 0, 1 or another byte against a state of 0, 1, 2 or
//! -1; an arena run of 0..5 section-12 entries (the length not a multiple
//! of 184 too) with random lamp bits; section B absent or with unk2C
//! negative, 0 or positive; D_800F1BA8 an arena palette. The CLUT stub
//! copies the 16 colours it is given into the arena (the C passes its own
//! table and the port its own: the address is not compared, the colours are).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008E4DC));

pub const classes = [_][:0]const u8{ "same", "lit", "unlit" };

pub const arg_masks = .{.{ "vs_main_loadClut", 0, 0 }};

pub const stubs = struct {
    pub fn vs_main_loadClut(ctx: *zd.Ctx, img: anytype, slot: anytype, offset: anytype, count: anytype) void {
        _ = slot;
        _ = offset;
        _ = count;
        const seen: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
        for (0..16) |k| seen[k] = img[k];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    v.g.D_800F1BA8().* = @ptrCast(@alignCast(ctx.allocBytes(0x240 * 2, 16)));
    v.g.D_800F1D6F().* = ctx.edge(u8, &.{ 0, 1, 2 });
    const n = ctx.range(u32, 0, 5);
    const entries = ctx.allocBytes(5 * @sizeOf(v._mpdRoomSection12), 16);
    psx.put(&room.section12, @as(*v._mpdRoomSection12, @ptrCast(@alignCast(entries))));
    room.header.section12Len = n * 184 + ctx.edge(u32, &.{ 0, 0, 1, 183 });
    if (ctx.chance(3)) {
        room.sectionB.raw = 0;
    } else {
        const b = ctx.alloc(v._mpdRoomSectionB);
        b.unk2C = ctx.pick(i32, &.{ -1, 0, 1, 0x7FFF_FFFF, -0x8000_0000 });
        psx.put(&room.sectionB, b);
    }
    return .{ctx.edge(c_int, &.{ 0, 1, 2, -1 })};
}

pub fn classify(args: Args) usize {
    if (@as(c_int, v.g.D_800F1D6F().*) == args[0]) return 0;
    return if (args[0] != 0) 1 else 2;
}
