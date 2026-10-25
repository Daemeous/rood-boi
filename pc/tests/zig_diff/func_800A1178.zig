//! zig_diff spec: func_800A1178 -> func_800A1178.func_800A1178 (30DB0.c).
//! Inputs: arg0 at the edges (0, -1, 1, INT_MIN, INT_MAX) or random, arg1 at
//! 0, 1, -1 or random, and a MATRIX and out word in the arena (each null one
//! time in eight; the C never tests them, it passes them on). func_800A152C's
//! stub returns 0 and -1 and both extremes often, plus random values, so its
//! result equals 0 in about a third of the runs and is negative in others.
//! Both paths are taken: a negative result is returned unchanged, and a
//! non-negative one calls func_800A11D8 and returns 0. func_800A11D8's stub
//! writes the matrix and the out word it is given (when non-null).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1178));

pub const classes = [_][:0]const u8{ "null-matrix", "buffers" };

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, id: anytype, flag: anytype, extra: anytype) c_int {
        _ = id;
        _ = flag;
        _ = extra;
        const r = ctx.value(u8);
        if (r < 90) return 0;
        if (r < 100) return -1;
        if (r < 110) return std.math.minInt(c_int);
        if (r < 120) return std.math.maxInt(c_int);
        return ctx.value(c_int);
    }

    pub fn func_800A11D8(ctx: *zd.Ctx, id: anytype, res: anytype, mtx: anytype, out: anytype) void {
        _ = id;
        _ = res;
        const m: ?*v.MATRIX = @ptrCast(@alignCast(mtx));
        if (m) |p| {
            p.m[0][0] = ctx.value(i16);
            p.t[0] = ctx.value(i32);
        }
        const o: ?*c_uint = @ptrCast(@alignCast(out));
        if (o) |p| p.* = ctx.value(c_uint);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1 });
    const mtx: ?*v.MATRIX = if (ctx.chance(8)) null else ctx.alloc(v.MATRIX);
    const out: [*c]c_uint = if (ctx.chance(8)) null else @ptrCast(ctx.alloc(c_uint));
    return .{ arg0, arg1, mtx, out };
}

pub fn classify(args: Args) usize {
    return if (args[2] == null) 0 else 1;
}
