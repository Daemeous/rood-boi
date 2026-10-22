//! zig_diff spec: func_800C0758 -> func_800C0758.func_800C0758 (573B8.c).
//! Inputs: D_800EB9B8 points into an arena block, 1 KB past its start, so
//! negative vertex indices stay inside memory the harness owns. phase covers
//! the int range up to 0x7FFFFF00 (no signed overflow in `i + phase + 8`),
//! with the sign edges. index is in [-64, 200]. segments is negative (the loop
//! is not entered), 0 (one vertex), 32 (the `segments != 32` edge) or 1..40.
//! The scratchpad trig and basis tables keep the harness's random bytes, so
//! both signs of the products and every `segments - i < 2` outcome occur.
//! `component` is only ever 0 or 1, so the `if (component && ...)` test never
//! sees a sign bit; that value flag is structural and cannot be made to differ.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0758));

pub const classes = [_][:0]const u8{ "no-vertices", "single-vertex", "segments-32", "multi-vertex" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The vertex buffer: D_800EB9B8 is a pointer the C reads, so point it at
    // arena memory with room before and after the struct for every index.
    const block: [*]u8 = ctx.allocBytes(4096, 16);
    const base: *v.D_800EB9B8_t = @ptrCast(@alignCast(block + 1024));
    v.g.D_800EB9B8().* = base;

    const edges = [_]c_int{ 0, -1, 1, -2147483648, 0x7FFF_FF00 };
    const phase: c_int = if (ctx.chance(2))
        edges[ctx.range(usize, 0, edges.len - 1)]
    else
        ctx.range(c_int, std.math.minInt(c_int), 0x7FFF_FF00);

    const segments: c_int = blk: {
        if (ctx.chance(6)) break :blk ctx.range(c_int, -5, -1);
        if (ctx.chance(6)) break :blk 32;
        if (ctx.chance(8)) break :blk 0;
        break :blk ctx.range(c_int, 1, 40);
    };

    const index: c_int = ctx.range(c_int, -64, 200);
    return .{ phase, segments, index };
}

pub fn classify(args: Args) usize {
    const segments = args[1];
    if (segments < 0) return 0;
    if (segments == 0) return 1;
    if (segments == 32) return 2;
    return 3;
}
