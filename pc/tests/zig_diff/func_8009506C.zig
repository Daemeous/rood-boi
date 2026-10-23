//! zig_diff spec: func_8009506C -> func_8009506C.func_8009506C (2842C.c).
//! Inputs: a D_800F1BAC_t with unkB in 0..30 (the step table's edges 0-4 and
//! 26-30 are picked half the time, so unkB == 4 and unkB == 27 hit their
//! comparisons often), arg0 around the 320 clip edge and the 0 edge (336 and 16
//! make the first comparisons EQUAL), arg1 anywhere in +-30000, the step and
//! clip tables with edge and random values, and four widths that include 0 so
//! `arg0 + w` meets 0. The scratchpad's prim pointer is a 4-poly arena block
//! and its ot pointer an arena block, so the C writes only memory the harness owns.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009506C));

pub const classes = [_][:0]const u8{ "step-low", "step-high", "step-mid", "clip-edge" };

pub const stubs = struct {
    // AddPrim links the primitive into the ordering table: it writes the
    // primitive's tag word (the C never reads it back, but a port must
    // still pass the same pointer and ot).
    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        _ = ot;
        const tag: *u32 = @ptrCast(@alignCast(p));
        tag.* = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F1BAC_t);
    obj.unkB = if (ctx.chance(2))
        ctx.edge(u8, &.{ 0, 3, 4, 26, 27, 28, 30 })
    else
        ctx.range(u8, 0, 30);

    const arg0: c_int = switch (ctx.range(u8, 0, 7)) {
        0 => 336,
        1 => 16,
        else => ctx.range(c_int, -400, 400),
    };
    const arg1: c_int = ctx.range(c_int, -30000, 30000);

    const t34 = v.g.D_800E8634();
    for (0..6) |k| t34[k] = ctx.pick(i16, &.{ 0, 1, -1, 16, 32767, -32768 });
    const t40 = v.g.D_800E8640();
    for (0..4) |k| {
        t40[k].w = ctx.pick(i16, &.{ 0, 0, 1, -1, 16, -16 });
        t40[k].x = ctx.int(i16);
        t40[k].y = ctx.int(i16);
    }

    // Four primitives' worth of room for the loop's writes, and an ot block.
    const polys: [*]v.POLY_FT4 = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.POLY_FT4) * 4, 16)));
    const ot = ctx.allocBytes(256, 16);
    const sp: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    psx.put(&sp.unk0, @as(*anyopaque, @ptrCast(polys)));
    psx.put(&sp.unk4, @as(*anyopaque, @ptrCast(ot + 64)));

    return .{ arg0, arg1, obj };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if (arg0 == 336 or arg0 == 16) return 3;
    const unkB = args[2].?.unkB;
    if (unkB < 4) return 0;
    if (unkB >= 27) return 1;
    return 2;
}
