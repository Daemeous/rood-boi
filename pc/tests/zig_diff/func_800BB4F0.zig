//! zig_diff spec: func_800BB4F0 -> func_800BB4F0.func_800BB4F0 (4A0A8.c).
//! Inputs: arg0 is a 16-byte block in the arena, or a block in the PS1
//! scratchpad (both valid for the byte the C reads, arg0[1]), with random
//! contents. arg1 is any c_short, which the C ignores. The C has no branches
//! and no calls, so the checks are the stored global (the random byte read
//! from arg0[1]) and the return value 0.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB4F0));

pub const classes = [_][:0]const u8{ "scratchpad", "arena-arg1-negative", "arena-arg1-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [*c]u8 = undefined;
    if (ctx.chance(4)) {
        // Inside the scratchpad, with room for arg0[1].
        p = @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, 0x800));
    } else {
        // An arena block, at any byte alignment the arena allows.
        p = ctx.allocBytes(16, 1);
    }
    const arg1: c_short = ctx.int(c_short);
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    if (@intFromPtr(args[0]) >= zd.scratchpad and @intFromPtr(args[0]) < zd.scratchpad + zd.scratchpad_size) return 0;
    return if (args[1] < 0) 1 else 2;
}
