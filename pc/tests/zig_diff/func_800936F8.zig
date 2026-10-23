//! zig_diff spec: func_800936F8 -> func_800936F8.func_800936F8 (2842C.c).
//! Inputs: the two bounds D_800F225A and D_800F225B are set to small edge
//! values (the second keeps 64 + bound within the 96-entry table), the
//! table's unk2 bytes are drawn from {0, 1, 2, 255} so `unk2 < 1U` sees
//! equality, and arg0 is drawn from the edges that make each comparison
//! equal (arg0 == bound, arg0 == 64, arg0 == 64 + bound, arg0 == 63 + bound),
//! plus negative indices (the C writes below the table for them) and
//! in-range values. No external calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800936F8));

pub const classes = [_][:0]const u8{ "negative-below-bound", "below-bound", "second-range", "outside" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bound_a = v.g.D_800F225A();
    const bound_b = v.g.D_800F225B();
    bound_a.* = ctx.edge(u8, &.{ 0, 1, 2, 50, 95, 96 });
    bound_b.* = ctx.edge(u8, &.{ 0, 1, 2, 31, 32 });

    const tbl = v.g.D_800F1DD8();
    var i: usize = 0;
    while (i < 96) : (i += 1) {
        tbl[i].unk2 = ctx.edge(u8, &.{ 0, 1, 1, 2, 255 });
    }

    const a: c_int = bound_a.*;
    const b: c_int = bound_b.*;
    const arg: c_int = switch (ctx.range(u8, 0, 9)) {
        0 => a,
        1 => 64,
        2 => 64 + b,
        3 => 63 + b,
        4 => -ctx.range(c_int, 1, 4),
        5 => ctx.range(c_int, 0, 95),
        6 => 63,
        7 => -1,
        else => a - 1,
    };
    return .{arg};
}

pub fn classify(args: Args) usize {
    const x: c_int = args[0];
    const a: c_int = v.g.D_800F225A().*;
    const b: c_int = v.g.D_800F225B().*;
    if (x < a) return if (x < 0) 0 else 1;
    if (x >= 64 and x < 64 + b) return 2;
    return 3;
}
