//! zig_diff spec: func_800913BC -> func_800913BC.func_800913BC (2842C.c).
//! Inputs: arg0 across the sign boundary, zero, the 16-bit wrap points and
//! the int extremes (so the `(short)` narrowing shows). The table index
//! D_800F1DC4 is 0..79, the table's length, and the table entry it selects
//! is zero or nonzero (nonzero bytes with and without the sign bit). The
//! callees are recording stubs; func_80045BFC may rewrite D_800F1DBC, which
//! the C never reads back, so a port that returns D_800F1DBC after the call
//! is caught.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800913BC));

pub const classes = [_][:0]const u8{ "negative", "zero-arg", "nonneg-table-zero", "nonneg-table-nonzero" };

pub const stubs = struct {
    pub fn func_80045BFC(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg0;
        _ = arg1;
        _ = arg2;
        _ = arg3;
        // The real callee may rewrite the global it was passed; the C does not read it back.
        if (ctx.value(u1) == 1) v.g.D_800F1DBC().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF, 0x8000, 0xFFFF, 0x10000, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const idx = ctx.range(i16, 0, 79);
    v.g.D_800F1DC4().* = idx;
    const table = v.g.D_80068EB4();
    table[@intCast(idx)] = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 255);
    return .{arg0};
}

pub fn classify(args: Args) usize {
    if (args[0] < 0) return 0;
    if (args[0] == 0) return 1;
    const idx: usize = @intCast(v.g.D_800F1DC4().*);
    if (v.g.D_80068EB4()[idx] == 0) return 2;
    return 3;
}
