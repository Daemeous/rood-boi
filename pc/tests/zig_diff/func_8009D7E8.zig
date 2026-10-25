//! zig_diff spec: func_8009D7E8 -> func_8009D7E8.func_8009D7E8 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 whose entry is either null or a
//! random object in the arena, and arg1 that is zero (often), negative,
//! positive, or an edge where arg1 * 2 wraps. The C reads only the table
//! entry it indexes, so every index it reads is one the table owns; indices
//! outside 0..16 are not built because the C would read memory past the table.
//! The C makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D7E8));

pub const classes = [_][:0]const u8{ "null-entry", "arg1-zero", "arg1-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx: c_int = ctx.range(c_int, 0, 16);
    if (ctx.chance(4)) {
        table[@intCast(idx)] = null;
    } else {
        const actor = ctx.alloc(v.D_800F4538_t);
        table[@intCast(idx)] = actor;
    }

    var arg1: c_int = undefined;
    if (ctx.chance(3)) {
        arg1 = 0;
    } else {
        arg1 = ctx.pick(c_int, &.{ 1, -1, 2, -2, 127, 128, 255, 256, -128, 0x40000000, -0x40000000, 0x7FFFFFFF, -0x80000000 });
    }
    return .{ idx, arg1 };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F4538()[@intCast(args[0])] == null) return 0;
    return if (args[1] == 0) 1 else 2;
}
