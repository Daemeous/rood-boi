//! zig_diff spec: func_80073ACC -> func_80073ACC.func_80073ACC (146C.c).
//! No inputs: three calls in order (the trace compares them).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80073ACC));

pub const classes = [_][:0]const u8{"any"};

pub fn prepare(_: *zd.Ctx) Args {
    return .{};
}

pub fn classify(_: Args) usize {
    return 0;
}
