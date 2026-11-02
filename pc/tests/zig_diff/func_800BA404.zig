//! zig_diff spec: func_800BA404 -> func_800BA404.func_800BA404 (4A0A8.c).
//! Inputs: a 3-byte buffer in the arena. Byte 1 selects a table entry: 0, 1
//! and 5 lie inside the 6-entry D_800E9B34 table, 6 and 7 just past it. Byte 2
//! is any u8, so values of 128 and up exercise the signed-slip case. The table
//! entries are random i32s and arg1 is unused by the C. The C has no branches;
//! its only effects are the call to func_80045D64 (a recording stub, which
//! changes nothing the C reads back) and the return value 0.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA404));

pub const classes = [_][:0]const u8{ "in-table", "past-table" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = ctx.allocBytes(3, 1);
    p[0] = ctx.int(u8);
    p[1] = ctx.edge(u8, &.{ 0, 1, 5, 6, 7 });
    p[2] = ctx.int(u8);

    const table = v.g.D_800E9B34();
    for (0..6) |i| table[i] = ctx.int(i32);

    return .{ p, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] < 6) 0 else 1;
}
