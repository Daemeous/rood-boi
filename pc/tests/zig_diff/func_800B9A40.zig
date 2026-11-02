//! zig_diff spec: func_800B9A40 -> func_800B9A40.func_800B9A40 (4A0A8.c).
//! Inputs: arg0 is a 4-byte block of random bytes (the C reads arg0[1..3] only
//! when the callee returns 0; it is never null, since the C does not test it),
//! and arg1 is any short. The callee func_8007C398 is chosen per run in prepare
//! so classify knows the class before the C runs: it returns 0, 5, or another
//! value that is neither 0 nor 5 (negative and positive values both occur).
//! Both tests of its result (!= 5 and == 0) are reached in every class.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9A40));

pub const classes = [_][:0]const u8{ "ret-zero", "ret-five", "ret-other" };

/// The class prepare chose for this iteration: what func_8007C398 returns.
var want: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    want = ctx.edge(u8, &.{ 0, 1, 2 });
    const block: [*c]u8 = @ptrCast(ctx.allocBytes(4, 1));
    return .{ block, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return want;
}

pub const stubs = struct {
    /// Returns the class's value: 0, 5, or (for "other") a random value that
    /// is neither 0 nor 5. func_8007C46C is a void callee the C never reads
    /// back, so it keeps the default recording stub.
    pub fn func_8007C398(ctx: *zd.Ctx) c_int {
        return switch (want) {
            0 => 0,
            1 => 5,
            else => blk: {
                const r = ctx.value(c_int);
                break :blk if (r == 0 or r == 5) -1 else r;
            },
        };
    }
};
