//! zig_diff spec: func_8008D2C0 -> func_8008D2C0.func_8008D2C0 (146C.c).
//! Inputs: no section 12, or an arena run of 0..5 entries (the length not a
//! multiple of 184 too) with random unk0_9 bits and coordinates at the
//! 4096 edges of both signs (the C divides, it does not shift);
//! D_800F1D70's entries 0..7 (7 reads the 0 past the 7-entry table); an arena output of 5 points.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008D2C0));

pub const classes = [_][:0]const u8{ "no-section", "empty", "entries" };

fn coordinate(ctx: *zd.Ctx) i32 {
    return ctx.pick(i32, &.{ 0, 4095, 4096, -4095, -4096, -4097, 0x7FFF_FFFF, -0x8000_0000 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    const points: [*c]v.func_8008D2C0_t = @ptrCast(@alignCast(ctx.allocBytes(5 * @sizeOf(v.func_8008D2C0_t), 16)));
    if (ctx.chance(6)) {
        room.section12.raw = 0;
        return .{points};
    }
    const n = ctx.range(u32, 0, 5);
    const entries: [*]v._mpdRoomSection12 = @ptrCast(@alignCast(ctx.allocBytes(5 * @sizeOf(v._mpdRoomSection12), 16)));
    for (0..5) |k| {
        entries[k].unkA8 = coordinate(ctx);
        entries[k].unkAC = coordinate(ctx);
        entries[k].unkB0 = coordinate(ctx);
        v.g.D_800F1D70()[k] = ctx.range(u8, 0, 7);
    }
    psx.put(&room.section12, @as(*v._mpdRoomSection12, &entries[0]));
    room.header.section12Len = n * 184 + ctx.edge(u32, &.{ 0, 0, 1, 183 });
    return .{points};
}

pub fn classify(_: Args) usize {
    const room = v.g.vs_battle_roomData();
    if (room.section12.raw == 0) return 0;
    return if (room.header.section12Len < 184) 1 else 2;
}
