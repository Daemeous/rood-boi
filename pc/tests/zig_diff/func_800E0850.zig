//! zig_diff spec: func_800E0850 -> func_800E0850.func_800E0850 (771A8.c).
//! Inputs: an arena func_800E0850_t object whose bytes are random (so
//! unk34/unk36 and the scratchpad's dx/dz bitfields vary), and an arg1 that is
//! an edge value half the time (0, 0x200, 0x3FF, 0x400, 0x600, bit 31 set,
//! all ones) or random: the low 10 bits pick one or three passes, bits 9-10
//! pick the first scratchpad entry. The first callee stub sometimes rewrites
//! unk34/unk36 (the C re-reads them after the call) and returns 0, 1, a
//! negative value or a random int; the second returns 0 often enough to give
//! `h == min` many times, and negative and positive values otherwise.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E0850));

pub const classes = [_][:0]const u8{ "single-pass", "triple-pass" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800E0850_t);
    const arg1 = ctx.pick(c_uint, &.{ 0, 0x200, 0x3FF, 0x400, 0x600, 0x7FF, 0x8000_0000, 0xFFFF_FFFF, 0x1FF });
    return .{ obj, arg1 };
}

pub fn classify(args: Args) usize {
    return if ((args[1] & 0x3FF) == 0) 0 else 1;
}

pub const stubs = struct {
    pub fn func_800E0678(ctx: *zd.Ctx, p: anytype, index: anytype) c_int {
        _ = index;
        if (ctx.value(u8) < 128) {
            // The real callee may move the offsets the C re-reads after the call.
            const a: *v.func_800E0850_t = @ptrCast(@alignCast(p));
            a.unk34 = ctx.value(u8);
            a.unk36 = ctx.value(u8);
        }
        const r = ctx.value(u8);
        if (r < 96) return 0;
        if (r < 160) return 1;
        if (r < 200) return -1;
        return ctx.value(c_int);
    }

    pub fn func_800E42EC(ctx: *zd.Ctx, x: c_int, z: c_int) c_int {
        _ = x;
        _ = z;
        const r = ctx.value(u8);
        if (r < 80) return 0;
        if (r < 130) return -@as(c_int, ctx.value(u8)) - 1;
        if (r < 170) return @as(c_int, ctx.value(u8));
        return ctx.value(c_int);
    }
};
