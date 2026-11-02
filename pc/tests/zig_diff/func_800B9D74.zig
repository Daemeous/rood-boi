//! zig_diff spec: func_800B9D74 -> func_800B9D74.func_800B9D74 (4A0A8.c).
//! Inputs: arg0 is a valid 16-byte arena buffer at a random offset 0-8, so
//! arg0 + 1 and arg0[3] are always readable and the byte offsets are not all
//! aligned; arg1 is an edge value (0, -1, 1, the extremes) or any c_short.
//! The C has no branch and no comparison: what it does is the call sequence
//! (getShort(arg0 + 1), then func_800BFE50, then func_800AC37C with arg0[3])
//! and the returned 0. The callees are default recording stubs;
//! vs_battle_getShort returns any value. Classes name the arg1 edge so every
//! input family is counted.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9D74));

pub const classes = [_][:0]const u8{ "arg1-zero", "arg1-negative", "arg1-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    const off = ctx.range(usize, 0, 8);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf + off, arg1 };
}

pub fn classify(args: Args) usize {
    const arg1 = args[1];
    if (arg1 == 0) return 0;
    if (arg1 < 0) return 1;
    return 2;
}
