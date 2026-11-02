//! zig_diff spec: func_800BEB9C -> func_800BEB9C.func_800BEB9C (4A0A8.c).
//! Inputs: arg0 from the edges (0, 1, -1, 63, 64, 127, 128, INT_MIN and any
//! random int), with arg0 == 0 forced one time in three so the loop runs
//! often. The C reads no globals and no objects. Its only effect is the
//! calls to vs_battle_setStateFlag(i, 0) for i = 64..127 in order, which the
//! harness records with their arguments. The callee returns nothing and its
//! effect lives in state this unit does not reach, so the default recording
//! stub is the model.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEB9C));

pub const classes = [_][:0]const u8{ "arg0-zero", "arg0-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) return .{0};
    return .{ctx.pick(c_int, &.{ 0, 1, -1, 63, 64, 127, 128, std.math.minInt(c_int) })};
}

pub fn classify(args: Args) usize {
    return if (args[0] == 0) 0 else 1;
}
