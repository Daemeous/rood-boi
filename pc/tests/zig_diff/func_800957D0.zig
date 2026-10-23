//! zig_diff spec: func_800957D0 -> func_800957D0.func_800957D0 (2842C.c).
//! Inputs: three c_int arguments (edges at the i16 boundaries, zero, -1 and
//! the bits shifted out of arg2, plus random values in a range that cannot
//! overflow the C's signed `-8`, `+16` and `+5` steps), and a scratchpad whose
//! first word points at a random POLY_FT4 in the arena (the primitive the C
//! fills) and whose second word points at an arena block (the ordering-table
//! pointer AddPrim receives). The C has no branch and no ordered comparison:
//! its value edges are the wrapping of arg0/arg1 and arg2 << 4 truncated into
//! the u8 and i16 fields of the primitive.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800957D0));

pub const classes = [_][:0]const u8{ "arg2 negative", "arg0 beyond i16", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Padding so the ordering-table pointer below is never before the arena start.
    _ = ctx.allocBytes(16, 16);
    const prim = ctx.alloc(v.POLY_FT4);
    const ot = ctx.alloc(v.POLY_FT4);

    // The scratchpad words the C reads: p[0] the primitive, p[1] the ot pointer.
    const sp: *[2]u32 = @ptrFromInt(zd.scratchpad);
    sp[0] = psx.addr(prim);
    sp[1] = psx.addr(ot);

    const a0: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 8, -8, 16, 0x7FFF, 0x8000, -0x8000, -0x8001, 0xFFFF, 0x10000, -1, 1 })
    else
        ctx.range(c_int, -(1 << 30), 1 << 30);

    const a1: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 5, -5, 0x7FFF, -0x8000, 0x10000, -1 })
    else
        ctx.range(c_int, -(1 << 30), 1 << 30);

    // arg2 << 4 must not overflow the shift: keep |arg2| below 2^27 (edges at the limit).
    const a2: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 1, -1, 0x7FFFFFF, -0x8000000, 0x40, 0x1000, -0x10 })
    else
        ctx.range(c_int, -(1 << 26), 1 << 26);

    return .{ a0, a1, a2 };
}

pub fn classify(args: Args) usize {
    if (args[2] < 0) return 0;
    if (args[0] < -32768 or args[0] > 32767) return 1;
    return 2;
}
