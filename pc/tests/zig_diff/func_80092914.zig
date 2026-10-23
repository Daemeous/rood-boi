//! zig_diff spec: func_80092914 -> func_80092914.func_80092914 (2842C.c).
//! The original body is empty and reads no argument, global or callee, so the
//! inputs are the scratchpad alone: the harness fills it with random bytes and
//! compares it after both runs. The unit has no branches and no value edges
//! to cover. The two input classes split on the sign bit of the first
//! scratchpad byte, which prepare sometimes forces, so both occur on every
//! run. The mutations add scratchpad stores (a byte, a read-modify-write word,
//! a half-word) that a port must not add or drop, and the random fill makes
//! each one differ from the untouched state.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092914));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *volatile u8 = @ptrFromInt(zd.scratchpad);
    if (ctx.chance(2)) scratch.* = ctx.value(u8) & 0x7F;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const scratch: *volatile u8 = @ptrFromInt(zd.scratchpad);
    return if (scratch.* & 0x80 != 0) 1 else 0;
}
