//! zig_diff spec: func_800AF844 -> func_800AF844.func_800AF844 (44F14.c).
//! Inputs: `count` from -3 to 5 (the loop body runs 0 to 5 times), with null
//! dst and src when the loop cannot run. Element pairs are built so that the
//! x and z differences land on the fold and clamp edges (0, +-2048, +-4096,
//! +-4097, +-8192, large values), so the sum x + z hits exactly 4096 often. dst
//! and src are sometimes the same array, which gives zero differences. Y and the
//! other coordinates take edge values, so the modulo and the negative results
//! are checked.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AF844));

pub const classes = [_][:0]const u8{ "no-elements", "aliased", "distinct" };

const deltas = [_]i32{ 0, 1, -1, 2047, 2048, -2048, 4095, 4096, -4095, -4096, 4097, -4097, 6144, -6144, 8192, -8192, 12288, 30000, -30000, 65535, -65535 };
const values = [_]i16{ 0, 1, -1, 4095, 4096, -4096, 8191, 8192, -8192, 32767, -32768 };

fn delta(ctx: *zd.Ctx) i32 {
    if (ctx.chance(4)) return ctx.range(i32, -65535, 65535);
    return deltas[ctx.range(usize, 0, deltas.len - 1)];
}

// Two i16 values whose difference (src - dst) is exactly d.
fn pair(ctx: *zd.Ctx, d: i32) [2]i16 {
    const lo: i32 = @max(-32768, -32768 - d);
    const hi: i32 = @min(32767, 32767 - d);
    const dst: i32 = ctx.range(i32, lo, hi);
    const dst16: i16 = @intCast(dst);
    const src16: i16 = @intCast(dst + d);
    return .{ dst16, src16 };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const count: c_int = if (ctx.chance(5)) ctx.range(c_int, -3, 0) else ctx.range(c_int, 1, 5);
    if (count <= 0 and ctx.chance(3)) return .{ null, null, count };

    const n: usize = @intCast(@max(count, 1));
    const dstArr: [*]v.SVECTOR = @ptrCast(@alignCast(ctx.allocBytes(n * @sizeOf(v.SVECTOR), 16)));
    const alias = count > 0 and ctx.chance(12);
    var srcArr: [*]v.SVECTOR = dstArr;
    if (!alias) srcArr = @ptrCast(@alignCast(ctx.allocBytes(n * @sizeOf(v.SVECTOR), 16)));

    for (0..n) |i| {
        if (alias) {
            dstArr[i].vx = ctx.pick(i16, &values);
            dstArr[i].vz = ctx.pick(i16, &values);
        } else {
            const px = pair(ctx, delta(ctx));
            const pz = pair(ctx, delta(ctx));
            dstArr[i].vx = px[0];
            srcArr[i].vx = px[1];
            dstArr[i].vz = pz[0];
            srcArr[i].vz = pz[1];
            srcArr[i].vy = ctx.pick(i16, &values);
        }
        dstArr[i].vy = ctx.pick(i16, &values);
    }

    const dp: *v.SVECTOR = @ptrCast(dstArr);
    const sp: *v.SVECTOR = @ptrCast(srcArr);
    return .{ dp, sp, count };
}

pub fn classify(args: Args) usize {
    if (args[2] <= 0) return 0;
    if (args[0] == args[1]) return 1;
    return 2;
}
