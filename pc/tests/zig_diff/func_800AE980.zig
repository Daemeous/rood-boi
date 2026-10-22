//! zig_diff spec: func_800AE980 -> func_800AE980.func_800AE980 (44F14.c).
//! Inputs: two D_800F4538_unkC54 objects in the arena, dst and src (the same
//! object one time in eight). The first 42 unk0 pairs are drawn so their
//! differences land on 0, +-1, +-3, +-4095, +-4096, +-4097, +-8192, +-65535
//! half the time and at random otherwise, so both sides of each `>=` and `<`
//! test are equal sometimes, the clamps and the odd negative halvings are
//! taken, and the i16 stores wrap. The unk150 arrays stay random. The count
//! runs from -3 to 60: empty loops, the 42-element edge, and indices past it
//! that still lie inside the struct.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE980));

pub const classes = [_][:0]const u8{ "empty", "aliased", "clamp-edge", "general" };

const delta_edges = [_]i32{ 0, 1, -1, 3, -3, 4095, -4095, 4096, -4096, 4097, -4097, 8192, -8192, 65535, -65535 };
const counts = [_]c_int{ -1, 0, 1, 2, 41, 42, 43, 59, 60 };

fn delta(ctx: *zd.Ctx) i32 {
    if (ctx.chance(2)) return delta_edges[ctx.range(usize, 0, delta_edges.len - 1)];
    return ctx.range(i32, -65535, 65535);
}

/// Two i16 values (a, b) with b - a == d, both in range.
fn pair(ctx: *zd.Ctx, d: i32) [2]i16 {
    const lo: i32 = @max(-32768, -32768 - d);
    const hi: i32 = @min(32767, 32767 - d);
    const base = ctx.range(i32, lo, hi);
    return .{ @intCast(base), @intCast(base + d) };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const dst = ctx.alloc(v.D_800F4538_unkC54);
    const src = if (ctx.chance(8)) dst else ctx.alloc(v.D_800F4538_unkC54);
    for (0..42) |i| {
        const px = pair(ctx, delta(ctx));
        const py = pair(ctx, delta(ctx));
        const pz = pair(ctx, delta(ctx));
        dst.unk0[i].vx = px[0];
        src.unk0[i].vx = px[1];
        dst.unk0[i].vy = py[0];
        src.unk0[i].vy = py[1];
        dst.unk0[i].vz = pz[0];
        src.unk0[i].vz = pz[1];
    }
    const count: c_int = if (ctx.chance(2))
        counts[ctx.range(usize, 0, counts.len - 1)]
    else
        ctx.range(c_int, -3, 60);
    return .{ dst, src, count };
}

pub fn classify(args: Args) usize {
    const dst = args[0].?;
    const src = args[1].?;
    const count = args[2];
    if (count <= 0) return 0;
    if (args[0] == args[1]) return 1;
    const n: usize = @intCast(@min(count, 42));
    for (0..n) |i| {
        const dx = @as(i32, src.unk0[i].vx) - @as(i32, dst.unk0[i].vx);
        const dy = @as(i32, src.unk0[i].vy) - @as(i32, dst.unk0[i].vy);
        const dz = @as(i32, src.unk0[i].vz) - @as(i32, dst.unk0[i].vz);
        if (@abs(dx) == 4096 or @abs(dy) == 4096 or @abs(dz) == 4096) return 2;
    }
    return 3;
}
