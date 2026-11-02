//! zig_diff spec: func_800704B0 -> func_800704B0.func_800704B0 (146C.c).
//! No branches: _cameraMode is random before and 6 after; func_800CF920 is a
//! recording stub (the call and the state hash at it are compared).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800704B0));

pub const classes = [_][:0]const u8{"always"};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g._cameraMode().* = ctx.pick(u32, &.{ 0, 5, 6, 7 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
