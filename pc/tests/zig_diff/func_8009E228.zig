//! zig_diff spec: func_8009E228 -> func_8009E228.func_8009E228 (30DB0.c).
//! Inputs: a null arg1 (with arg0 null or valid), or a random SVECTOR with
//! vx, vy, vz from edge values, against a D_800F4538_t with an edge
//! position. Differences up to 65535 square past INT_MAX, so the squares
//! wrap negative and the sign-fix branches run; some differences are exactly
//! zero so the `< 0` tests see equal sides. func_800A1AF8 fills its
//! out-vector from ctx.value (all three fields, so the C never reads
//! uninitialised stack), and ratan2 returns any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E228));

pub const classes = [_][:0]const u8{ "null-svector", "square-x-wraps", "square-z-wraps", "normal" };

pub const stubs = struct {
    pub fn func_800A1AF8(ctx: *zd.Ctx, base: anytype, k: anytype, out: anytype, flag: anytype) void {
        _ = base;
        _ = k;
        _ = flag;
        const o: *v.SVECTOR = @ptrCast(@alignCast(out));
        o.vx = ctx.value(i16);
        o.vy = ctx.value(i16);
        o.vz = ctx.value(i16);
    }

    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F4538_t);
    const arg1 = ctx.alloc(v.SVECTOR);
    const pos = &arg0.unk0.position;
    pos.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    pos.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    pos.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    arg0.unk0.unkF = ctx.int(u8);
    if (ctx.chance(6)) return .{ if (ctx.chance(2)) null else arg0, null };
    arg1.vx = if (ctx.chance(4)) pos.vx else ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 0x4000, -0x4000 });
    arg1.vy = ctx.int(i16);
    arg1.vz = if (ctx.chance(4)) pos.vz else ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 0x4000, -0x4000 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const arg1 = args[1] orelse return 0;
    const pos = args[0].?.unk0.position;
    const dx: i32 = @as(i32, arg1.vx) - @as(i32, pos.vx);
    if (dx *% dx < 0) return 1;
    const dz: i32 = @as(i32, arg1.vz) - @as(i32, pos.vz);
    if (dz *% dz < 0) return 2;
    return 3;
}
