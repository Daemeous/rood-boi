//! zig_diff spec: func_800D4AB4 -> func_800D4AB4.func_800D4AB4 (5BF94.c).
//! Inputs: a fresh D_800F53B8_t in the arena and the index global D_800F522C
//! set to three kinds of value: below 4 (the store runs, at indices 0-3), exactly
//! 4 (the `<` sides are equal, the store is skipped), and above 4 (skipped).
//! The table D_800EC32C is 4 bytes of random fill, so each index reads a
//! distinct byte. The return value is always 1.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4AB4));

pub const classes = [_][:0]const u8{ "in-range", "at-four", "above-four" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F53B8_t);
    // Equal to the bound a third of the time, below it or above it otherwise.
    const index: u8 = if (ctx.chance(3))
        4
    else if (ctx.chance(2))
        ctx.range(u8, 0, 3)
    else
        ctx.range(u8, 5, 255);
    v.g.D_800F522C().* = index;
    return .{arg0};
}

pub fn classify(args: Args) usize {
    _ = args;
    const x = v.g.D_800F522C().*;
    if (x < 4) return 0;
    if (x == 4) return 1;
    return 2;
}
