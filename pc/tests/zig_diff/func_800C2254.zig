//! zig_diff spec: func_800C2254 -> func_800C2254.func_800C2254 (58578.c).
//! Inputs: an arena D_800EB9B8_t that the global points at, and an angle and
//! index around the ranges the C reaches. The index is negative (the vertex
//! pointer lands inside the object, since index * 8 + 0x48 >= 0 for index >= -9),
//! zero, or positive up to 2000. The angle covers the edge values and any
//! i32 that keeps angle + 32 * 128 below INT_MAX, so the C itself never
//! overflows. rcos and rsin return values in the real table range
//! -4096..4095, so the products with the random scratchpad basis fit in an int.
//! The scratchpad basis is random, so both signs of the products occur.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C2254));

pub const classes = [_][:0]const u8{ "index-negative", "index-zero", "index-positive" };

const edge_angles = [_]c_int{ 0, 127, 128, -128, -1, 0x7FFF_DFFF, -0x7FFF_FFFF };

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(u13)) - 4096;
    }
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(u13)) - 4096;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const base = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = base;
    const angle: c_int = if (ctx.chance(3))
        edge_angles[ctx.range(usize, 0, edge_angles.len - 1)]
    else
        ctx.range(c_int, -0x7FFF_FFFF, 0x7FFF_DFFF);
    const index: c_int = if (ctx.chance(4))
        ctx.range(c_int, -9, -1)
    else if (ctx.chance(3))
        0
    else
        ctx.range(c_int, 1, 2000);
    return .{ angle, index };
}

pub fn classify(args: Args) usize {
    const index = args[1];
    if (index < 0) return 0;
    if (index == 0) return 1;
    return 2;
}
