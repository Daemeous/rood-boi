//! zig_diff spec: pc_charge_skip_reseed -> charge_skip.reseed (146C.c, charge_skip.inc).
//! Inputs: a seed (srand and rand are recording stubs: the pool takes the 97 values rand gives).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").reseed));

pub const classes = [_][:0]const u8{"reseed"};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_uint, &.{ 0, 1, 0x7FFF_FFFF, 0xFFFF_FFFF })};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
