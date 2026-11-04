//! zig_diff spec: func_800E4C1C -> mark_cell_occupied.markCellOccupied (D2: pc/src/battle_movement.c).
//! Inputs: D_800F58B8 null a fifth of the time, else the address of a 32x33
//! grid of 16-bit cells of random bytes (a row is 64 bytes), and a cell x and
//! z in 0..31 (31 half the time); the cell's 0x80 bit is set or clear as the
//! random bytes fall, so the OR is told from an overwrite or a toggle.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").markCellOccupied));

pub const classes = [_][:0]const u8{ "no-grid", "bit-clear", "bit-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(5)) {
        v.g.D_800F58B8().* = 0;
    } else {
        const grid = ctx.allocBytes(33 * 64, 16);
        for (grid[0 .. 33 * 64]) |*byte| byte.* = ctx.int(u8);
        v.g.D_800F58B8().* = @intCast(@intFromPtr(grid));
    }
    const x: c_int = if (ctx.chance(2)) 31 else ctx.range(c_int, 0, 31);
    const z: c_int = if (ctx.chance(2)) 31 else ctx.range(c_int, 0, 31);
    return .{ x, z };
}

pub fn classify(args: Args) usize {
    const grid = v.g.D_800F58B8().*;
    if (grid == 0) return 0;
    const cell: *align(1) const u16 = @ptrFromInt(grid + @as(usize, @intCast(args[0])) * 2 + @as(usize, @intCast(args[1])) * 64);
    return if (cell.* & 0x80 != 0) 2 else 1;
}
