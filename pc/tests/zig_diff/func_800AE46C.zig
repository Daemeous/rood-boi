//! zig_diff spec: func_800AE46C -> func_800AE46C.func_800AE46C (44F14.c).
//! The C body is empty: no arguments, no calls, no branches, no globals. The
//! inputs are the random state the harness already fills (arena, scratchpad,
//! low RAM, every global the unit reaches), and every state byte is compared
//! after the run, so one call per iteration is the whole spec. Mutations write
//! fixed scratchpad bytes and copy one scratchpad byte to another, so a port
//! that leaves the scratchpad alone is caught.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE46C));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
