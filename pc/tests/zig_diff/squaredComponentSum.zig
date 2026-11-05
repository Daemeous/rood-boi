//! zig_diff spec: func_800E4660 -> pc.squared_component_sum.squaredComponentSum (D2: pc/src/boot_sdk.c).
//! Inputs: three ints, small, short-range or any int (the GTE takes their signed
//! halves). MTC2, SQR and MFC2 are recording stubs: the arguments and the three
//! MFC2 values, summed with wrapping, are what show.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").squaredComponentSum));

pub const classes = [_][:0]const u8{ "small", "wide" };

fn component(ctx: *zd.Ctx, wide: bool) c_int {
    if (wide) return ctx.pick(c_int, &.{ 0x8000, -0x8001, 0x10000, 0x7fffffff, -0x80000000 });
    return ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff, -0x8000, 100 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const wide = ctx.chance(2);
    return .{ component(ctx, wide), component(ctx, wide), component(ctx, wide) };
}

pub fn classify(args: Args) usize {
    inline for (args) |a| if (a < -0x8000 or a > 0x7fff) return 1;
    return 0;
}
