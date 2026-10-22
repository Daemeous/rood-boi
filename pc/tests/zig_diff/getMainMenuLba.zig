//! zig_diff spec: vs_battle_getMainMenuLba -> get_main_menu_lba.getMainMenuLba (6F014.c).
//! The function takes no arguments, reads no object or global, calls nothing and
//! has no branch: it returns the constant 85000. Every iteration therefore runs the
//! same input, and the comparison checks the return value alone.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getMainMenuLba));

pub const classes = [_][:0]const u8{"no-input"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
