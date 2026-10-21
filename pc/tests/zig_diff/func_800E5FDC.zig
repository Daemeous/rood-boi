//! zig_diff spec: func_800E5FDC -> func_800E5FDC.func_800E5FDC (7CD68.c).
//! Inputs: the four table entries' dx and dz (every i2 value), the two
//! arguments near the 0..31 grid with x == 32 and z == 32 exactly in about a
//! quarter of the runs each, and edges (-1, -2, the 32-bit ends, 0x7FFFFFFE
//! and its negative, which wrap when the entry's offset is added). The grid
//! is an arena block of random u16 cells, so the mask test sees both zero and
//! nonzero masked values, with and without bit 15. The target iteration is
//! steered: iterations before it fail the mask, the target passes, and the
//! target is drawn from 0..4 (4 = no hit, so the loop falls through to -1).
//! func_8008DC7C is a default int stub returning a random c_short, so the
//! result's sign reaches the >> 6. It writes nothing the C reads after the
//! call: the C returns at once.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5FDC));

pub const classes = [_][:0]const u8{ "hit-0", "hit-1", "hit-2", "hit-3", "no-hit" };

/// An axis argument: sometimes exactly 32 - d (so the entry's x or z is 32),
/// sometimes an edge, otherwise a value near the grid.
fn axis(ctx: *zd.Ctx, d: i2) c_int {
    if (ctx.chance(4)) return 32 - @as(c_int, d);
    if (ctx.chance(8)) return ctx.pick(c_int, &.{ -1, -2, -3, 31, 32, 33, 0x7FFFFFFE, -0x7FFFFFFE, 0, 1 });
    return ctx.range(c_int, -2, 33);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const tab = v.g.D_800F16EC();
    for (tab) |*e| {
        psx.setBits(e, "dx", ctx.int(i2));
        psx.setBits(e, "dz", ctx.int(i2));
    }

    // The grid the C indexes once x and z pass the bound checks.
    const grid = ctx.alloc([32][32]u16);
    for (grid) |*row| {
        for (row) |*cell| cell.* = ctx.int(u16);
    }
    const rows: [*][32]u16 = grid;
    v.g.D_800F58B8().* = rows;

    const arg0 = axis(ctx, psx.getBits(&tab[0], "dx"));
    const arg1 = axis(ctx, psx.getBits(&tab[0], "dz"));

    // Steer the first hit: entries before t fail the mask, entry t passes it.
    const t = ctx.range(usize, 0, 4);
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const e = &tab[i * 2];
        const x: u32 = @bitCast(arg0 +% @as(c_int, psx.getBits(e, "dx")));
        const z: u32 = @bitCast(arg1 +% @as(c_int, psx.getBits(e, "dz")));
        if (x < 32 and z < 32) {
            if (i < t) {
                grid[z][x] |= 0x70;
            } else if (i == t) {
                grid[z][x] &= ~@as(u16, 0x70);
            }
        }
    }
    return .{ arg0, arg1 };
}

/// Replays the C's loop on the prepared state: the index of the entry that hits, or 4.
pub fn classify(args: Args) usize {
    const tab = v.g.D_800F16EC();
    const grid = v.g.D_800F58B8().*.?;
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const e = &tab[i * 2];
        const x: u32 = @bitCast(args[0] +% @as(c_int, psx.getBits(e, "dx")));
        const z: u32 = @bitCast(args[1] +% @as(c_int, psx.getBits(e, "dz")));
        if (x < 32 and z < 32 and (grid[z][x] & 0x70) == 0) return i;
    }
    return 4;
}
