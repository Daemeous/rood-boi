//! zig_diff spec: vs_battle_playSfx10 -> play_sfx10.playSfx10 (573B8.c).
//! The C takes no arguments and has no branch: it makes one call,
//! vs_main_playSfxDefault(0x7E, 10), and returns. The inputs are the
//! scratchpad and arena fill the harness randomises every iteration. The
//! recording stub for the callee does nothing, so the trace (its arguments
//! and the state hash at the call) is what the port must match.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").playSfx10));

pub const classes = [_][:0]const u8{"always"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
