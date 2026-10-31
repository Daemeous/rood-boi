//! zig_diff spec: _nop1 -> nop1.nop1 (146C.c).
//! The C body is empty: no arguments, no branches, no calls, and no object or
//! global it reads. The inputs are therefore just the random state the harness
//! fills (arena, globals, scratchpad, low RAM), and the check is that the port
//! also touches nothing. The single input class covers that one case. The
//! mutations add a store to the scratchpad or to PS1 address 0, which the
//! state comparison must catch.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").nop1));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
