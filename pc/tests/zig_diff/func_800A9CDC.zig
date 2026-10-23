//! zig_diff spec: func_800A9CDC -> func_800A9CDC.func_800A9CDC (40564.c).
//! Inputs: an index 0..16 into D_800F4538 (both ends included), with every
//! table entry a valid D_800F4538_t: the indexed one holds `mine`, the rest
//! hold `other`, so a porter who reads a neighbour sees different bytes. The
//! unk5C0 fields take the signed/unsigned edges (0x7FFF, 0x8000, 0xFFFF). arg2
//! is -1 half the time (the branch that reads unk5C0) and otherwise an edge
//! value (0, 1, -2, 0xFFFF, the c_int extremes). arg1 is null one time in four.
//! The callee stub writes one byte of arg1 when it is non-null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9CDC));

pub const classes = [_][:0]const u8{ "arg2-minus-one", "arg2-explicit" };

/// The callee writes into arg1 (what the real func_800AA490 can change). The
/// C reads new_var->unk5C0 before the call and nothing after it, so the
/// write is checked through the state comparison rather than read back.
pub const stubs = struct {
    pub fn func_800AA490(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg0;
        _ = arg2;
        _ = arg3;
        const fields: *v.func_8006EBF8_t_fields = @ptrCast(@alignCast(arg1 orelse return));
        fields._bits_0[ctx.value(u2)] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const arg0: c_int = if (ctx.chance(3))
        ctx.edge(c_int, &.{ 0, 16 })
    else
        ctx.range(c_int, 0, 16);

    const mine = ctx.alloc(v.D_800F4538_t);
    const other = ctx.alloc(v.D_800F4538_t);
    const edges = [_]u16{ 0, 1, 0x7FFF, 0x8000, 0xFFFF, 0xFFFE };
    mine.unk5C0 = ctx.pick(u16, &edges);
    other.unk5C0 = ctx.pick(u16, &edges);

    const index: usize = @intCast(arg0);
    for (table, 0..) |*slot, i| {
        slot.* = if (i == index) mine else other;
    }

    const arg1: ?*v.func_8006EBF8_t_fields = if (ctx.chance(4))
        null
    else
        ctx.alloc(v.func_8006EBF8_t_fields);

    const arg2: c_int = if (ctx.chance(2))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, -2, 0xFFFF, std.math.maxInt(c_int), std.math.minInt(c_int) });

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    return if (args[2] == -1) 0 else 1;
}
