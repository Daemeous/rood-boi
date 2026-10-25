//! zig_diff spec: func_8009E614 -> func_8009E614.func_8009E614 (30DB0.c).
//! The C is a single forwarding call, `func_8009E700(arg0, 6)`, with no
//! branches, no globals and no reads after the call. The inputs cover
//! arg0 at the forwarded constant's neighbours (5, 6, 7), zero, -1, the
//! int extremes and random values, so a wrong argument, a wrong constant
//! or a dropped call shows in the recorded trace. The callee is a recording
//! stub: nothing the C reads comes back from it, so it needs no behaviour.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E614));

pub const classes = [_][:0]const u8{ "is-six", "negative", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.pick(c_int, &.{
        0,
        5,
        6,
        7,
        -1,
        std.math.minInt(c_int),
        std.math.maxInt(c_int),
    });
    return .{a};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (a == 6) return 0;
    if (a < 0) return 1;
    return 2;
}
