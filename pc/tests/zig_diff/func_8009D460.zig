//! zig_diff spec: func_8009D460 -> func_8009D460.func_8009D460 (30DB0.c).
//! The C body is empty, so there is no branch, value edge or callee to cover.
//! The inputs are the empty argument tuple, and the harness's random fill of the
//! arena, the globals, the scratchpad and PS1 RAM at address 0 is what the run
//! compares. The mutations add stores to the scratchpad and to low RAM, which the
//! random fill would catch.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D460));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
