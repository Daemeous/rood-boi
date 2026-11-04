//! zig_diff spec: func_80098B38 -> pc.room_light_at.roomLightAt (D2: pc/src/room_lighting.c).
//! Inputs: the room data block's enabled byte (0x00, any positive, 0x80..0xFF:
//! it is read signed) and its triangle table pointer at +0x74 (an arena block,
//! or null a quarter of the time), a position word (any), a layer (0, 1 or
//! any int) and a colour word in the arena. The stub of pc_room_light records
//! all five arguments, so the table address, the signed enabled flag and the
//! layer are each compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_room_lighting");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").roomLightAt));

pub const classes = [_][:0]const u8{ "enabled-zero", "enabled-positive", "enabled-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    room[0] = ctx.edge(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    const table: u32 = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(ctx.allocBytes(64, 16)));
    std.mem.writeInt(u32, room[0x74..][0..4], table, .little);
    // The word at +0x78 is a table pointer too (aligned, or null), so a port reading
    // it (mutation R5) hands the stub another address rather than a misaligned one.
    const other: u32 = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(ctx.allocBytes(64, 16)));
    std.mem.writeInt(u32, room[0x78..][0..4], other, .little);
    const color: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    return .{
        ctx.int(c_uint),
        ctx.edge(c_int, &.{ 0, 1, -1, 2, 0x7FFFFFFF }),
        color,
    };
}

pub fn classify(args: Args) usize {
    _ = args;
    const enabled: i8 = @bitCast(v.g.vs_battle_roomData()[0]);
    return if (enabled == 0) 0 else if (enabled > 0) 1 else 2;
}
