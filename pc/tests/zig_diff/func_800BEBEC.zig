//! zig_diff spec: func_800BEBEC -> func_800BEBEC.func_800BEBEC (4A0A8.c).
//! The C body is empty: no calls, no branches, no arguments. The inputs are
//! the harness's random state (the arena, the scratchpad, low RAM and the
//! globals), and every iteration must leave that state unchanged in both runs.
//! The mutations write to the scratchpad, to low RAM and to address 0, so the
//! state comparison and the crash check are exercised.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEBEC));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
