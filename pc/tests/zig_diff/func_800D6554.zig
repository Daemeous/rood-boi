//! zig_diff spec: func_800D6554 -> func_800D6554.func_800D6554 (5BF94.c).
//! No inputs: one call, func_8006CE50(), a recording stub.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6554));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
