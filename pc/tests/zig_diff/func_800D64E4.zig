//! zig_diff spec: func_800D64E4 -> func_800D64E4.func_800D64E4 (5BF94.c).
//! No inputs: one call, func_800CF830(0, 0), a recording stub.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D64E4));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
