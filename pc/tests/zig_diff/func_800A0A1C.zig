//! zig_diff spec: func_800A0A1C -> func_800A0A1C.func_800A0A1C (30DB0.c).
//! Inputs: an index into D_800F4538 in 0..16 (the table's bounds, the edges
//! included), and arg1 from the edges 0, +-1, +-2, 3 and the int extremes.
//! The table entry is null, a live arena object, or the PS1 address 0 mapping
//! (zd.low_ram, 0x80000000: a pointer with the sign bit set that the harness
//! owns and compares). The C writes bit 0 of arg1 into unk5AC_9 and bit 1
//! (the arithmetic shift) into unk5AC_11, so negative values are covered too.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0A1C));

pub const classes = [_][:0]const u8{ "null-entry", "lowram-entry", "arena-entry-negative", "arena-entry-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const arg0: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 1, 15, 16 })
    else
        ctx.range(c_int, 0, 16);
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, -2, std.math.minInt(c_int), std.math.maxInt(c_int) });
    const obj = ctx.alloc(v.D_800F4538_t);
    const entry: ?*v.D_800F4538_t = if (ctx.chance(4))
        null
    else if (ctx.chance(3))
        @ptrFromInt(zd.low_ram)
    else
        obj;
    table[@intCast(arg0)] = entry;
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const entry = v.g.D_800F4538()[@intCast(args[0])];
    const p = entry orelse return 0;
    if (@intFromPtr(p) == zd.low_ram) return 1;
    return if (args[1] < 0) 2 else 3;
}
