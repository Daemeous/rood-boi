//! zig_diff spec: func_800DB4AC -> pc.enter_movement_mode.enterMovementMode (D2: pc/src/battle_movement.c).
//! Inputs: a random-filled state block; a mode among the handled ones (1, 3,
//! 5, 6), unhandled small ones, and the wrapping extremes of `mode * 2` (the
//! byte at 0x474 + mode * 2 is cleared); byte 0x8A (the current mode) equal to
//! the mode a third of the time; for mode 5 the waypoint table (D_800F58D8 +
//! byte 0x101 * 16) with entries that are flagged (bit 15) or not, edge values
//! (0, -1, 0x7FFF, -0x8000), and copies of earlier entries so equal distances
//! occur, against an actor tile near or far from the entries' tiles. The stub
//! of func_800E4BB8 records the state it is handed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").enterMovementMode));

pub const classes = [_][:0]const u8{ "same-mode", "mode-1", "mode-3", "mode-6", "mode-5-chosen", "mode-5-none", "other-mode" };

var chosen: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x600, 16);
    for (block[0..0x600]) |*byte| byte.* = ctx.int(u8);
    const mode = ctx.edge(c_int, &.{ 1, 1, 3, 3, 5, 5, 5, 6, 6, 0, 2, 4, 7, 8, 0x7FFFFFFF, -0x80000000, -1, 100, -100 });
    if (ctx.chance(3)) block[0x8a] = @truncate(@as(c_uint, @bitCast(mode)));
    // Mode 5 reads the 8 halfwords at D_800F58D8 + byte 0x101 * 16.
    const index = ctx.range(u8, 0, 3);
    block[0x101] = index;
    const table = ctx.allocBytes(0x100, 16);
    v.g.D_800F58D8().* = @intCast(@intFromPtr(table) - @as(usize, index) * 16);
    const tile = if (ctx.chance(2)) ctx.int(u32) else ctx.int(u32) & 0x001F001F;
    std.mem.writeInt(u32, block[0x34..][0..4], tile, .little);
    chosen = false;
    const none = ctx.chance(4);
    for (0..8) |i| {
        var entry: u16 = switch (ctx.range(u8, 0, 5)) {
            0 => 0,
            1 => 0xFFFF,
            2 => 0x7FFF,
            3 => 0x8000,
            else => ctx.int(u16) | (if (ctx.chance(2)) @as(u16, 0x8000) else 0),
        };
        if (i != 0 and ctx.chance(4)) entry = std.mem.readInt(u16, table[2 * ctx.range(usize, 0, i - 1) ..][0..2], .little);
        if (none) entry &= 0x7FFF;
        std.mem.writeInt(u16, table[2 * i ..][0..2], entry, .little);
        if (entry & 0x8000 != 0) chosen = true;
    }
    return .{ block, mode };
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (@as(c_int, state[0x8a]) == args[1]) return 0;
    return switch (args[1]) {
        1 => 1,
        3 => 2,
        6 => 3,
        5 => if (chosen) 4 else 5,
        else => 6,
    };
}
