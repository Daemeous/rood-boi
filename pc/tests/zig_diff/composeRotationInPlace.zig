//! zig_diff spec: func_800B196C -> pc.compose_rotation.composeRotationInPlace (D2: pc/src/boot_sdk.c, through its static compose_rotation).
//! Inputs: two MATRIX blocks of random bytes, the same block a quarter of the
//! time. The GTE and SetRotMatrix are recording stubs (arguments and the state
//! at each call): the three columns' vectors, in order, and the MFC2 values
//! stored back are what show.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_boot_sdk");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").composeRotationInPlace));

pub const classes = [_][:0]const u8{ "distinct", "same" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(c.MATRIX);
    return .{ a, if (ctx.chance(4)) a else ctx.alloc(c.MATRIX) };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] == args[1]);
}
