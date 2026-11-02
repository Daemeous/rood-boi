//! zig_diff spec: func_800BA218 -> func_800BA218.func_800BA218 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena, so arg0 and arg0+1 / arg0+3 are
//! valid addresses, and a random arg1 that the C never reads (the port must
//! not read it either), taking negative, zero and positive values. The two
//! vs_battle_getShort calls return any 16-bit value, negative ones included,
//! and the call trace records each pointer and each value passed on to
//! func_80091468, so the offsets, the argument order and the narrowing are
//! all checked.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA218));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-zero", "arg1-positive" };

pub const stubs = struct {
    /// Reads a 16-bit value from the guest buffer; the value is whatever the
    /// stub draws, the same in both runs.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    const arg1 = args[1];
    if (arg1 < 0) return 0;
    if (arg1 == 0) return 1;
    return 2;
}
