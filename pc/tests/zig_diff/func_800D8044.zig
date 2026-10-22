//! zig_diff spec: func_800D8044 -> func_800D8044.func_800D8044 (6F014.c).
//! The C body is one return of the global D_800F5680. Inputs: that word set
//! to edge values (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF) half the time and
//! random bytes otherwise, so both the sign bit and the high half are set and
//! clear across runs. The function takes no arguments and calls nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8044));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F5680().* = ctx.pick(u32, &.{ 0, 1, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F5680().* & 0x8000_0000 != 0) 1 else 0;
}
