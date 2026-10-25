//! zig_diff spec: func_800A0AFC -> func_800A0AFC.func_800A0AFC (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 whose entry is an arena object or
//! null (the C has no bounds check, so the index stays inside the table), and
//! two ints drawn from edges that cover zero, +-1, the 4096 divisor
//! boundaries, the INT_MIN/INT_MAX extremes and negatives (so the truncating
//! `/` and the signed `>=` are both exercised). arg2 equals arg1 a third of
//! the time so the comparison sees equal sides often.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0AFC));

pub const classes = [_][:0]const u8{ "absent", "present-ge", "present-lt" };

const edges = [_]c_int{
    0,              1,         -1,         4095,
    4096,           -4095,     -4096,      4097,
    -4097,          8191,      8192,       0x7FFF_FFFF,
    -0x7FFF_FFFF - 1, 0x1000_0000, -0x1000_0000,
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    const idx = ctx.range(c_int, 0, 16);
    table[@intCast(idx)] = if (ctx.chance(3)) null else obj;
    const arg1 = ctx.pick(c_int, &edges);
    const arg2 = if (ctx.chance(3)) arg1 else ctx.pick(c_int, &edges);
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    _ = table[@intCast(args[0])] orelse return 0;
    return if (args[1] >= args[2]) 1 else 2;
}
