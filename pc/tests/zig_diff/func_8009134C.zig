//! zig_diff spec: func_8009134C -> func_8009134C.func_8009134C (2842C.c).
//! Inputs: two c_ints drawn from edge values (0, +-1, the i16 sign boundary,
//! 0xFFFF, 0x10000, the int extremes) or any random int. The C stores them as
//! i16 into the last two entries of D_800F1D98 and then calls func_80090B28.
//! The stub of that callee writes entry 2, so the state a later call sees is
//! observable; the table's other entries keep their random fill.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009134C));

pub const classes = [_][:0]const u8{ "arg0-nonneg-arg1-nonneg", "arg0-nonneg-arg1-neg", "arg0-neg-arg1-nonneg", "arg0-neg-arg1-neg" };

pub const stubs = struct {
    /// The callee changes entry 2 of the table, which the C never reads back;
    /// the state hash taken at the call still shows any store moved past it.
    pub fn func_80090B28(ctx: *zd.Ctx) void {
        const d = v.g.D_800F1D98();
        d[2] = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const edges = [_]c_int{
        0,      1,      -1,     0x7FFF, 0x8000, -0x8000, -0x8001,
        0xFFFF, 0x10000, std.math.maxInt(c_int), std.math.minInt(c_int),
    };
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    const a0: usize = if (args[0] < 0) 2 else 0;
    const a1: usize = if (args[1] < 0) 1 else 0;
    return a0 + a1;
}
