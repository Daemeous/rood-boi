//! zig_diff spec: _atan2FixedPoint -> atan2_fixed_point.atan2FixedPoint (4A0A8.c).
//! Inputs: both arguments drawn from the edges of c_int (0, +-1, the values
//! around +-64 where `>> 6` rounds toward minus infinity, 127/128, INT_MIN,
//! INT_MAX), plus uniformly random values. The C has no branches and reads no
//! state; the only effect to compare is the call to ratan2 (its arguments and
//! the returned value, which the default int stub draws from ctx.value).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").atan2FixedPoint));

pub const classes = [_][:0]const u8{ "both-negative", "y-negative", "x-negative", "both-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const edges = [_]c_int{
        0,                       1,                 -1,   63,  64,  -64, -65,
        127,                     128,               -128, 255, 256, -256,
        std.math.maxInt(c_int),  std.math.minInt(c_int),
    };
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    const y = args[0] < 0;
    const x = args[1] < 0;
    if (y and x) return 0;
    if (y) return 1;
    if (x) return 2;
    return 3;
}
