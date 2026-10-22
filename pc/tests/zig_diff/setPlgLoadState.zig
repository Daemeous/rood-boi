//! zig_diff spec: vs_battle_setPlgLoadState -> set_plg_load_state.setPlgLoadState (6F014.c).
//! The C is a single store of its argument into the global `_plgLoadState`, with no
//! branches, calls or comparisons. The inputs are the edge values of c_int (zero, one,
//! minus one, the 16-bit and 32-bit limits, the 0x10000 carry) plus random values, so
//! every sign class and every width the store could truncate is compared against the port.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setPlgLoadState));

pub const classes = [_][:0]const u8{ "negative", "zero", "positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{
        0,
        1,
        -1,
        0x7FFF,
        0x8000,
        0xFFFF,
        0x10000,
        std.math.minInt(c_int),
        std.math.maxInt(c_int),
    })};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    if (x < 0) return 0;
    if (x == 0) return 1;
    return 2;
}
