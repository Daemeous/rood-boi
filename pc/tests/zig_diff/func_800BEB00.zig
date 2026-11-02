//! zig_diff spec: func_800BEB00 -> func_800BEB00.func_800BEB00 (4A0A8.c).
//! The C is a single return of the global D_800F4C34 (an unsigned byte,
//! widened to int). The inputs are that byte across its whole range, with
//! the edges 0, 1, 127, 128 and 255 drawn often, so a port that sign-extends
//! it (values with the top bit set) or mixes up the low bits is caught.
//! There are no arguments, calls, branches or ordered comparisons. The two
//! classes split the runs by the top bit of the returned byte.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEB00));

pub const classes = [_][:0]const u8{ "top-bit-clear", "top-bit-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The byte the C returns: random fill already covers every value, so
    // half the runs pin it to the edges that matter for widening.
    v.g.D_800F4C34().* = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C34().* & 0x80 == 0) 0 else 1;
}
