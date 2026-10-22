//! zig_diff spec: func_800C0990 -> func_800C0990.func_800C0990 (573B8.c).
//! Inputs: two SVECTORs in the arena whose depth ((vz0 + vz1) >> 1) runs from
//! negative through 2047 and 2048 and up (the unsigned depth test, and the
//! ordering-table slot for every depth 0..2047), and whose x and y deltas are
//! zero, equal in magnitude, x-major or y-major (the octant tests and the
//! D_80040A14 lookup at indices 0..512). vs_scratch's primitive and ordering
//! table are arena blocks. The colour runs over its edges, and the intensity
//! over values around 0 and 64 (the clamp edges). D_80040A14 keeps its random
//! bytes: it is data the C only reads.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0990));

pub const classes = [_][:0]const u8{ "depth-out", "point", "diagonal", "x-major", "y-major" };

/// A random coordinate delta: zero, small, large or medium.
fn delta(ctx: *zd.Ctx) i32 {
    return switch (ctx.range(u8, 0, 4)) {
        0 => 0,
        1 => ctx.range(i32, -40, 40),
        2 => ctx.range(i32, -65535, 65535),
        else => ctx.range(i32, -600, 600),
    };
}

/// A start coordinate on one axis such that start + d fits an i16.
fn place(ctx: *zd.Ctx, d: i32) i16 {
    const lo: i32 = @max(-32768, -32768 - d);
    const hi: i32 = @min(32767, 32767 - d);
    return @intCast(ctx.range(i32, lo, hi));
}

/// Start and end on one axis with end - start == d.
fn pair(ctx: *zd.Ctx, d: i32) [2]i16 {
    const s = place(ctx, d);
    return .{ s, @intCast(@as(i32, s) + d) };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const start = ctx.alloc(v.SVECTOR);
    const end = ctx.alloc(v.SVECTOR);
    const prim = ctx.alloc([8]u32);
    const table = ctx.alloc([2048]u32);
    const scratch = v.g.vs_scratch();
    psx.put(&scratch.unk0, prim);
    psx.put(&scratch.unk4, table);

    // Depth sums: edges around 4095/4096 (depth 2047/2048), negatives, and random.
    const sums = [_]i32{ 0, 2047, 4094, 4095, 4096, 4097, 4098, -1, -2, 65534, -65536 };
    const sum: i32 = if (ctx.chance(3)) sums[ctx.range(usize, 0, sums.len - 1)] else ctx.range(i32, -2000, 4300);
    const vlo: i32 = @max(-32768, sum - 32767);
    const vhi: i32 = @min(32767, sum + 32768);
    const svz = ctx.range(i32, vlo, vhi);
    start.vz = @intCast(svz);
    end.vz = @intCast(sum - svz);

    // Deltas: zero, equal magnitude (diagonal), or free.
    const dx = delta(ctx);
    const dy: i32 = switch (ctx.range(u8, 0, 5)) {
        0 => dx,
        1 => -dx,
        2 => 0,
        else => delta(ctx),
    };
    const xs = pair(ctx, dx);
    const ys = pair(ctx, dy);
    start.vx = xs[0];
    end.vx = xs[1];
    start.vy = ys[0];
    end.vy = ys[1];

    const colors = [_]c_uint{ 0, 0xFF, 0x00FF_FFFF, 0x0100_0000, 0x8000_0000, 0xFFFF_FFFF, 0x1234_5678 };
    const color: c_uint = ctx.pick(c_uint, &colors);
    const intensity: c_int = ctx.pick(c_int, &.{ 0, 1, 63, 64, 65, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });

    return .{ start, end, color, intensity };
}

pub fn classify(args: Args) usize {
    const start = args[0].?;
    const end = args[1].?;
    const depth: i32 = (@as(i32, start.vz) + @as(i32, end.vz)) >> 1;
    if (@as(u32, @bitCast(depth)) >= 2048) return 0;
    const dx = @as(i32, end.vx) - @as(i32, start.vx);
    const dy = @as(i32, end.vy) - @as(i32, start.vy);
    if (dx == 0 and dy == 0) return 1;
    if (@abs(dx) == @abs(dy)) return 2;
    return if (@abs(dx) > @abs(dy)) 3 else 4;
}
