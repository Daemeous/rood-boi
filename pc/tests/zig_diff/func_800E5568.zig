//! zig_diff spec: func_800E5568 -> func_800E5568.func_800E5568 (7CD68.c).
//! Inputs: an arena object with every shape 0-7 (shapes 1, 2, 4 take rangeZ
//! through rcos; shape 3 takes rangeY through rcos; shapes 0, 5, 6, 7 take
//! rangeY through rsin), angle 0-31 (so angle << 7 covers its whole range),
//! and range bytes at 0, 1, 127, 128 and 255. The rcos and rsin stubs return
//! a zero about 8% of the time (so `v >= 0` sees equal sides) and a
//! signed 16-bit value otherwise, so both signs of `v` occur and the
//! product cannot overflow.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5568));

pub const classes = [_][:0]const u8{ "shape-rcos-z", "shape-rcos-y", "shape-rsin" };

fn sample(ctx: *zd.Ctx) c_int {
    if (ctx.value(u8) < 20) return 0;
    return @as(c_int, ctx.value(i16));
}

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        return sample(ctx);
    }
    pub fn rsin(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        return sample(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800E5568_t);
    obj.rangeY = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    obj.rangeZ = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    psx.setBits(obj, "shape", ctx.range(u3, 0, 7));
    psx.setBits(obj, "angle", ctx.range(u5, 0, 31));
    return .{obj};
}

pub fn classify(args: Args) usize {
    const shape = psx.getBits(args[0].?, "shape");
    if (shape == 1 or shape == 2 or shape == 4) return 0;
    if (shape == 3) return 1;
    return 2;
}
