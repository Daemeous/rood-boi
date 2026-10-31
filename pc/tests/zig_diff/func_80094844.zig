//! zig_diff spec: func_80094844 -> func_80094844.func_80094844 (2842C.c).
//! Inputs: a position (pos, drawn from the clamp edges 31-33, 199-201, 287-289,
//! the i16 extremes, or a moderate range) and a label whose unk10/unk12 pair is
//! the 0x1000 reset trigger (both, only one, or neither, with offsets at 0, -1,
//! 0x7FFF and -0x8000 so the adds wrap). The list D_800F1CBC is an arena array
//! with room below its start, and D_800F227C is the count: 0 and the negative
//! counts (-1, -2, which the store at index count still writes), a scatter of
//! 1-8 random entries, a scatter with entry 0 placed on the position at the
//! overlap edges (dx, dy = 0, 1, 9, 10, 23, 24 and their negatives), a dense
//! grid of 90 entries that covers every search candidate (so no free spot is
//! found), and the same grid with random holes (so a spot is sometimes found).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80094844));

pub const classes = [_][:0]const u8{ "no-entries", "sparse", "overlap", "dense-holes", "dense-full" };

/// The class of the last prepared input (set by prepare, read by classify).
var mode: usize = 0;

const label_offs = [_]i16{ 0, 0x0FFF, 0x1001, -1, 0x7FFF, -0x8000, 32, -32 };
const pos_edges = [_]i16{ 31, 32, 33, 199, 200, 201, 287, 288, 289, -1, 0, 0x7FFF, -0x8000 };
const ovx = [_]i16{ 0, 1, -1, 23, -23, 24, -24, 25 };
const ovy = [_]i16{ 0, 1, -1, 9, -9, 10, -10, 11, -11 };

fn placePos(ctx: *zd.Ctx, pos: [*c]c_short) void {
    pos[0] = if (ctx.chance(2)) ctx.pick(i16, &pos_edges) else ctx.range(i16, 0, 320);
    pos[1] = if (ctx.chance(2)) ctx.pick(i16, &pos_edges) else ctx.range(i16, 0, 240);
}

/// A grid of entries around the position covering every search candidate;
/// with holes, each entry is dropped one time in four.
fn fillGrid(dv: [*]v.DVECTOR, pos: [*c]c_short, ctx: *zd.Ctx, holes: bool) i16 {
    var n: usize = 0;
    var k: i32 = -3;
    while (k <= 6) : (k += 1) {
        var m: i32 = -2;
        while (m <= 6) : (m += 1) {
            if (holes and ctx.chance(4)) continue;
            dv[n].vx = @intCast(@as(i32, pos[0]) + 16 * k);
            dv[n].vy = @intCast(@as(i32, pos[1]) + 8 * m);
            n += 1;
        }
    }
    return @intCast(n);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const label = ctx.alloc(v.D_800F1BAC_t);

    // The list: 256 entries, with 16 bytes below the start so a count of -1 or -2 stays inside the arena.
    const base: [*]u8 = ctx.allocBytes(256 * 4 + 32, 16) + 16;
    const dv: [*]v.DVECTOR = @ptrCast(@alignCast(base));
    v.g.D_800F1CBC().* = @ptrCast(base);

    const pos: [*c]c_short = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));

    // The reset trigger: both 0x1000 in three of six draws, one of them in one more.
    const lk = ctx.range(u8, 0, 5);
    label.unk10 = if (lk < 4) 0x1000 else ctx.pick(i16, &label_offs);
    label.unk12 = if (lk < 3) 0x1000 else ctx.pick(i16, &label_offs);

    const sel = ctx.range(u8, 0, 9);
    mode = switch (sel) {
        0 => 0,
        1, 2, 3 => 1,
        4, 5, 6 => 2,
        7, 8 => 3,
        else => 4,
    };

    var n: i16 = 0;
    switch (mode) {
        0 => {
            // No entries (or a negative count): the search is skipped.
            n = ctx.edge(i16, &.{ 0, -1, -2 });
            placePos(ctx, pos);
        },
        1 => {
            // Scattered entries, usually no overlap with the position.
            n = ctx.range(i16, 1, 8);
            placePos(ctx, pos);
            var k: usize = 0;
            while (k < @as(usize, @intCast(n))) : (k += 1) {
                dv[k].vx = ctx.range(i16, -300, 300);
                dv[k].vy = ctx.range(i16, -200, 200);
            }
        },
        2 => {
            // Entry 0 on the position at the overlap edges: the search runs.
            n = ctx.range(i16, 1, 8);
            placePos(ctx, pos);
            var k: usize = 0;
            while (k < @as(usize, @intCast(n))) : (k += 1) {
                dv[k].vx = ctx.range(i16, -300, 300);
                dv[k].vy = ctx.range(i16, -200, 200);
            }
            dv[0].vx = @truncate(@as(i32, pos[0]) + ctx.edge(i16, &ovx));
            dv[0].vy = @truncate(@as(i32, pos[1]) + ctx.edge(i16, &ovy));
        },
        3 => {
            pos[0] = ctx.range(i16, 0, 320);
            pos[1] = ctx.range(i16, 0, 240);
            n = fillGrid(dv, pos, ctx, true);
        },
        else => {
            pos[0] = ctx.range(i16, 0, 320);
            pos[1] = ctx.range(i16, 0, 240);
            n = fillGrid(dv, pos, ctx, false);
        },
    }
    v.g.D_800F227C().* = n;

    return .{ pos, label };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
