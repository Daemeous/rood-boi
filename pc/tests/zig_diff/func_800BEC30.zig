//! zig_diff spec: func_800BEC30 -> func_800BEC30.func_800BEC30 (4A0A8.c).
//! Inputs: the flag D_800F4C34 is 1 half the time (the equal side of the
//! comparison) and otherwise 0, 2, 0x80 or 0xFF, so both branches run. The
//! store to D_800F4BB8 happens only in the equal case, and D_800F4BB8 keeps
//! its random fill, so a store of the wrong value or a dropped store shows.
//! The function makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEC30));

pub const classes = [_][:0]const u8{ "flag-one", "flag-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const flag = v.g.D_800F4C34();
    flag.* = if (ctx.chance(2)) 1 else ctx.pick(u8, &.{ 0, 2, 0x80, 0xFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C34().* == 1) 0 else 1;
}
