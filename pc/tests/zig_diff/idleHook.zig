//! zig_diff spec: func_800E6EAC -> pc.idle_hook.idleHook (D2: pc/src/battle_helpers.c).
//! No input: the function must leave every compared byte and make no call.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").idleHook));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
