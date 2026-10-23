//! zig_diff spec: func_80092540 -> func_80092540.func_80092540 (2842C.c).
//! The C takes no arguments, reads no globals or objects, makes no calls and
//! has no branches: its body is `return 0;`. Every iteration runs it on the
//! same (empty) input with random state around it, so the check is that the
//! port returns the same value and touches no state the C does not.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092540));

pub const classes = [_][:0]const u8{"always"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
