//! zig_diff spec: func_80094E18 -> func_80094E18.func_80094E18 (2842C.c).
//! Inputs: a D_800F1BAC_t in the arena with unkE in 3..8, so the index into
//! D_800E861C stays in its six entries, whose values are 0..32 so the rect
//! index stays in D_80068FAC's 33 entries. unkC is random with the sign bit
//! both set and clear. displayTextLen and the rect widths are random, so
//! the width can be negative. x and y cover the i16 boundaries and ordinary
//! ranges. The scratchpad's prim and ordering-table pointers point into the
//! arena. AddPrim is a recording stub that writes the prim's tag word.

const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80094E18));

pub const classes = [_][:0]const u8{ "sign-set", "sign-clear" };

pub const stubs = struct {
    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        _ = ot;
        const tag: *u32 = @ptrCast(@alignCast(p.?));
        tag.* = ctx.value(u32);
    }
};

fn coord(ctx: *zd.Ctx) c_int {
    if (ctx.chance(2)) return ctx.edge(c_int, &.{ 0x7FFF, 0x8000, -0x8000, -0x8001, 0xFFFF, 0x10000, 0, -1 });
    return ctx.range(c_int, -70000, 70000);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    const p = ctx.alloc(v.D_800F1BAC_t);
    const prim0: *v.POLY_FT4 = @ptrCast(@alignCast(ctx.allocBytes(2 * @sizeOf(v.POLY_FT4), 16)));
    const ot: *anyopaque = @ptrCast(ctx.allocBytes(64, 16) + 20);
    psx.put(&scratch.unk0, prim0);
    psx.put(&scratch.unk4, ot);

    const tbl = v.g.D_800E861C();
    for (0..6) |i| tbl[i] = ctx.range(i32, 0, 32);

    p.unkE = ctx.range(u8, 3, 8);
    p.unkC = ctx.pick(u16, &.{ 0x8000, 0x7FFF, 0, 0xFFFF });

    const x = coord(ctx);
    const y = coord(ctx);
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    return if ((args[2].?.unkC & 0x8000) != 0) 0 else 1;
}
