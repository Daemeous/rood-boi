//! zig_diff spec: func_800BB7C4 -> func_800BB7C4.func_800BB7C4 (4A0A8.c).
//! Inputs: any arg0 (edges 0, -1, INT_MIN, INT_MAX, else random, so both signs
//! occur) and an arena SVECTOR with random contents. The C has no branches; the
//! value edges come from the callees' outputs. func_800A1178 writes the matrix
//! and the psx_long out-parameter, RotTransPers writes the packed screen xy
//! (sp30[0]) and the p/flag words, and func_800A1280 may rewrite the vector.
//! Each write is a ctx.value draw, so the port must read these back in the
//! same order. ratan2, SetRotMatrix and SetTransMatrix are recording stubs
//! whose arguments are compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB7C4));

pub const classes = [_][:0]const u8{ "arg0-negative", "arg0-nonnegative" };

pub const stubs = struct {
    pub fn func_800A1178(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, m: anytype, out: anytype) c_int {
        _ = arg0;
        _ = arg1;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (0..3) |i| {
            for (0..3) |j| mat.m[i][j] = ctx.value(i16);
        }
        for (0..3) |i| mat.t[i] = ctx.value(i32);
        const o: *c_int = @ptrCast(@alignCast(out));
        o.* = ctx.value(c_int);
        return ctx.value(c_int);
    }

    pub fn RotTransPers(ctx: *zd.Ctx, v0: anytype, sxy: anytype, p: anytype, flag: anytype) c_int {
        _ = v0;
        const s: *c_int = @ptrCast(@alignCast(sxy));
        s.* = ctx.value(c_int);
        const pp: *c_int = @ptrCast(@alignCast(p));
        pp.* = ctx.value(c_int);
        const f: *c_int = @ptrCast(@alignCast(flag));
        f.* = ctx.value(c_int);
        return ctx.value(c_int);
    }

    pub fn func_800A1280(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, vec: anytype, k: anytype) c_int {
        _ = arg0;
        _ = arg1;
        _ = k;
        if (ctx.value(u8) < 128) {
            const sv: *v.SVECTOR = @ptrCast(@alignCast(vec));
            sv.vx = ctx.value(i16);
            sv.vy = ctx.value(i16);
            sv.vz = ctx.value(i16);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const vec = ctx.alloc(v.SVECTOR);
    const arg0 = ctx.pick(c_int, &.{ 0, -1, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{ arg0, vec };
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 0 else 1;
}
