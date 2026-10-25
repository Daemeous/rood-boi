//! zig_diff spec: func_800A0B5C -> func_800A0B5C.func_800A0B5C (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538. Every table entry is set to one arena
//! object, and the selected entry is null about one time in five. arg1 and arg2
//! come from an edge set (0, +-1, 2, the u16 wrap points 65535/65536, INT_MIN,
//! INT_MAX) so both sides of each `< 0` test are equal and the stores see
//! wrapping values. The object keeps random bytes, so the stores into unk5C2 and
//! unk5C0 are visible and a missing store shows up.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0B5C));

pub const classes = [_][:0]const u8{ "null-entry", "neg-arg1", "neg-arg2", "plain" };

const edges = [_]c_int{ 0, 1, -1, 2, 65535, 65536, -65536, -2147483648, 2147483647 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    for (table) |*e| e.* = obj;
    const idx = ctx.range(c_int, 0, 16);
    if (ctx.chance(5)) table[@intCast(idx)] = null;
    const a1 = ctx.pick(c_int, &edges);
    const a2 = ctx.pick(c_int, &edges);
    return .{ idx, a1, a2 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    if (table[@intCast(args[0])] == null) return 0;
    if (args[1] < 0) return 1;
    if (args[2] < 0) return 2;
    return 3;
}
