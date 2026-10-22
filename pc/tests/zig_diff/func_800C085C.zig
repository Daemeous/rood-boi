//! zig_diff spec: func_800C085C -> func_800C085C.func_800C085C (573B8.c).
//! Inputs: a 4-byte scale block in the arena, its bytes drawn from the edges
//! 0, 1, 0x7F, 0x80 and 0xFF or at random, so the unsigned reads, the
//! `<< 5` results and the negated `scale[3] << 4` all see sign-bit and
//! boundary values. The angle comes from the edges around the i16 limits,
//! 0x10000 and the c_int limits, so the narrowing store into the short wraps
//! in some runs and not in others. RotMatrixYXZ_gte and ApplyMatrixSV are
//! recording stubs: the rotation matrix and each ApplyMatrixSV output are
//! written with ctx.value, because the C reads the outputs back in the
//! transpose loop and the port must too.
const std = @import("std");
const zd = @import("zdiff");
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");
const c = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C085C));

pub const classes = [_][:0]const u8{ "angle-fits-i16", "angle-wraps-i16" };

pub const stubs = struct {
    pub fn RotMatrixYXZ_gte(ctx: *zd.Ctx, r: anytype, m: anytype) [*c]c.MATRIX {
        _ = r;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (0..3) |i| {
            for (0..3) |j| {
                mat.m[i][j] = ctx.value(i16);
            }
        }
        mat.t = .{ ctx.value(i32), ctx.value(i32), ctx.value(i32) };
        return null;
    }

    pub fn ApplyMatrixSV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) [*c]c.SVECTOR {
        _ = m;
        _ = v0;
        const out: *v.SVECTOR = @ptrCast(@alignCast(v1));
        out.vx = ctx.value(i16);
        out.vy = ctx.value(i16);
        out.vz = ctx.value(i16);
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scale = ctx.allocBytes(4, 1);
    for (0..4) |i| {
        scale[i] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    }
    const angle = ctx.pick(c_int, &.{
        0,      1,                     -1,
        0x7FFF, -0x8000,               0x8000,
        0xFFFF, 0x10000,               -0x10001,
        std.math.maxInt(c_int), std.math.minInt(c_int),
    });
    return .{ scale, angle };
}

pub fn classify(args: Args) usize {
    const narrow: c_int = @as(i16, @truncate(args[1]));
    return if (narrow == args[1]) 0 else 1;
}
