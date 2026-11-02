//! zig_diff spec: func_800BD2B8 -> func_800BD2B8.func_800BD2B8 (4A0A8.c).
//! Inputs: a random MATRIX (only passed to SetRotMatrix/SetTransMatrix, whose
//! stubs read nothing from it) and a per-iteration mode for the height stub:
//! ties (small results, so the comparisons see equal sides often), wide random
//! 16-bit results (both signs of the 15-bit shift), or the 15-bit sign edges
//! (-16384, 16383). RotTrans writes pos from its input vector, so the four
//! corner offsets reach the C through pos.vx and pos.vz.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BD2B8));

pub const classes = [_][:0]const u8{ "ties", "wide", "sign-edges" };

var mode: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.range(usize, 0, 2);
    return .{ctx.alloc(v.MATRIX)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}

pub const stubs = struct {
    pub fn RotTrans(ctx: *zd.Ctx, v0: anytype, v1: anytype, flag: anytype) void {
        const src: *const v.SVECTOR = @ptrCast(@alignCast(v0));
        const dst: *v.VECTOR = @ptrCast(@alignCast(v1));
        const f: *c_int = @ptrCast(@alignCast(flag));
        dst.vx = ctx.value(i32) +% @as(i32, src.vx);
        dst.vy = ctx.value(i32);
        dst.vz = ctx.value(i32) +% @as(i32, src.vz) *% 3;
        f.* = ctx.value(c_int);
    }

    pub fn func_8008DC7C(ctx: *zd.Ctx, a: c_int, b: c_int) c_short {
        return switch (mode) {
            0 => @as(c_short, @intCast(ctx.value(u8) % 3)) - 1,
            1 => @truncate(a +% b),
            else => if (ctx.value(u1) == 1) @as(c_short, 16383) else @as(c_short, -16384),
        };
    }
};
