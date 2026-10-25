//! zig_diff spec: func_8009D458 -> func_8009D458.func_8009D458 (30DB0.c).
//! The original body is empty (`void func_8009D458(void) { }`): it takes no
//! arguments, reads no global, calls nothing and has no branch. The inputs
//! are therefore the empty argument tuple, and the check is that the port
//! also leaves every state byte (the arena, the globals and the scratchpad)
//! unchanged. The mutations write into the scratchpad and PS1 low RAM, which
//! the harness compares, so a port that stores anything is caught.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D458));

pub const classes = [_][:0]const u8{"empty"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
