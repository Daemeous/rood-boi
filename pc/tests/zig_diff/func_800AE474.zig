//! zig_diff spec: func_800AE474 -> func_800AE474.func_800AE474 (44F14.c).
//! The C body is empty: no arguments, no branches, no calls, no return value.
//! Nothing needs building, so prepare returns the empty argument tuple. There
//! is a single input class, which always occurs. Every iteration still
//! randomises the arena, the globals, the scratchpad and low RAM, and the
//! comparison of those bytes is what the mutations test: each one writes into
//! the scratchpad or PS1 address 0 and must show up as a mismatch against the
//! port, which writes nothing.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE474));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
