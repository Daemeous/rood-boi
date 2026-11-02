//! zig_diff spec: func_800D2518 -> func_800D2518.func_800D2518 (5BF94.c).
//! No arguments: D_800F54D8 and D_800F5520 keep the harness's random bytes;
//! the copy is compared byte for byte.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D2518));

pub const classes = [_][:0]const u8{"copy"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
