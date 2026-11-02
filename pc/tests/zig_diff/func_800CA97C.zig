//! zig_diff spec: func_800CA97C -> func_800CA97C.func_800CA97C (5BF94.c).
//! No arguments: D_800EB9B4 and D_800F4ED8 start with the harness's random
//! bytes (memset is libc's, not a stub).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CA97C));

pub const classes = [_][:0]const u8{"clear"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
