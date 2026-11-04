//! zig_diff spec: func_800E4478 -> pc.navigation_floor.belowFloor (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled 0x1000-byte arena block whose middle (+0x400) is the
//! cell grid the scratchpad word at 0x1F8003BC points at (64-byte rows of
//! halfword cells), a position (x, z) whose cell lies inside it (a little
//! below zero included, so the shifts are arithmetic), cells with bit 13 set
//! (floor 0) or clear (the floor from the low 10 bits and bits 10-12), and a
//! height y that is the cell's floor, one off it, or any int.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").belowFloor));

pub const classes = [_][:0]const u8{ "no-floor", "below", "not-below" };

var which: usize = 0;

fn floorOf(cell: u32) i32 {
    if (cell & 0x2000 != 0) return 0;
    const height: i32 = @as(i32, @bitCast(cell << 22)) >> 16;
    const sub: i32 = @intCast((cell & 0x1c00) >> 3);
    return @as(i32, @bitCast(@as(u32, @bitCast(height - sub)) << 17)) >> 17;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x1000, 16);
    for (block[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    const grid: usize = 0x400;
    const scratch: *u32 = @ptrFromInt(0x1F8003BC);
    scratch.* = @intCast(@intFromPtr(block) + grid);
    const x = ctx.range(c_int, -256, 4095);
    const z = ctx.range(c_int, -256, 1023);
    const offset: isize = @as(isize, (z >> 7) * 64) + @as(isize, (x >> 7) * 2);
    const at: usize = @intCast(@as(isize, @intCast(grid)) + offset);
    if (ctx.chance(3)) {
        const cell: u16 = ctx.int(u16) | 0x2000;
        std.mem.writeInt(u16, block[at..][0..2], cell, .little);
    } else if (ctx.chance(2)) {
        const cell: u16 = ctx.int(u16) & ~@as(u16, 0x2000);
        std.mem.writeInt(u16, block[at..][0..2], cell, .little);
    }
    const cell = std.mem.readInt(u16, block[at..][0..2], .little);
    const floor = floorOf(cell);
    const y: c_int = switch (ctx.range(u8, 0, 4)) {
        0 => floor,
        1 => floor -% 1,
        2 => floor +% 1,
        3 => floor,
        else => ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 }),
    };
    which = if (cell & 0x2000 != 0) 0 else if (y < floor) 1 else 2;
    return .{ x, y, z };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
