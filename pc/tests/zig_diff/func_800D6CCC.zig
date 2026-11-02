//! zig_diff spec: func_800D6CCC -> func_800D6CCC.func_800D6CCC (5BF94.c).
//! Inputs: an arena block of 12 random words; the first eight are cleared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6CCC));

pub const classes = [_][:0]const u8{"clear"};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{@as([*c]c_int, @ptrCast(@alignCast(ctx.allocBytes(48, 16))))};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
