//! zig_diff spec: func_800AE68C -> func_800AE68C.func_800AE68C (44F14.c).
//! Inputs: arg0 is an index into D_800F4538 (0..16). Every entry of the
//! table is set to its own valid arena object, so the C's pointer read never
//! faults and a mutation that reads another entry sees a different pointer.
//! arg1 is a spread of ints, kept away from INT_MAX so arg1 + 4 never
//! overflows. The callee func_800AE4FC is a stub that rewrites fields of the
//! unk0 block it is passed; the C never reads them back, but a port that
//! does must still agree with the C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE68C));

pub const classes = [_][:0]const u8{ "first-entry", "last-entry", "middle-entry" };

pub const stubs = struct {
    pub fn func_800AE4FC(ctx: *zd.Ctx, block: anytype, n: anytype) void {
        _ = n;
        const u: *v.D_800F4538_unk0 = @ptrCast(@alignCast(block));
        if (ctx.value(u1) == 1) u.pitch = ctx.value(i16);
        if (ctx.value(u1) == 1) u.roll = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    for (tbl) |*e| e.* = ctx.alloc(v.D_800F4538_t);
    const idx = ctx.range(c_int, 0, 16);
    const arg1: c_int = if (ctx.chance(2))
        ctx.pick(c_int, &.{ 0, -4, -1, 1, 2147483643, -2147483648, -2147483644 })
    else
        ctx.range(c_int, -2147483600, 2147483600);
    return .{ idx, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    if (args[0] == 16) return 1;
    return 2;
}
