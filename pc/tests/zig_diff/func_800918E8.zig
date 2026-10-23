//! zig_diff spec: func_800918E8 -> func_800918E8.func_800918E8 (2842C.c).
//! Inputs: the global D_800F1DBC (i16) is 0, -1, 1 or an i16 extreme, so both
//! sides of `>= 0` are often equal and both signs occur. D_800F1DC4 is an index
//! in 0..79, the table's room, so every read stays inside the harness's table.
//! The table bytes keep their random fill (every u8 value, high bit included).
//! arg0 is 0, 1, -1 or any c_int, so `!= 0` and the sign are both exercised.
//! func_80045BFC is void and nothing is read after it, so it needs no stub:
//! the default recording stub covers it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800918E8));

pub const classes = [_][:0]const u8{ "global-negative", "arg-zero", "arg-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F1DBC().* = ctx.edge(i16, &.{ 0, 0, -1, 1, 32767, -32768 });
    v.g.D_800F1DC4().* = ctx.range(i16, 0, 79);
    return .{ctx.pick(c_int, &.{ 0, 1, -1 })};
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F1DBC().* < 0) return 0;
    return if (args[0] == 0) 1 else 2;
}
