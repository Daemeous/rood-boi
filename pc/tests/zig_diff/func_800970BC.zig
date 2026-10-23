//! zig_diff spec: func_800970BC -> func_800970BC.func_800970BC (2842C.c).
//! Inputs: a POLY_FT4 in the arena, pointed to by scratchpad word 0, and an
//! ordering-table slot in the arena, pointed to by scratchpad word 1 (the
//! C subtracts 5 words from it). The D_800F2280 offsets are drawn from
//! edge values, so the -11/+11 and -32/0 offsets wrap i16 at the ends of its
//! range. AddPrim is a recording stub that writes the ordering-table slot
//! and the primitive's tag, as the real one does.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800970BC));

pub const classes = [_][:0]const u8{ "plain", "x-edge" };

pub const stubs = struct {
    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        const o: ?*anyopaque = ot;
        const q: ?*anyopaque = p;
        const w: *align(1) u32 = @ptrCast(o.?);
        w.* = ctx.value(u32);
        const pr: *v.POLY_FT4 = @ptrCast(@alignCast(q.?));
        pr.tag = (pr.tag & 0xFF00_0000) | (ctx.value(u32) & 0x00FF_FFFF);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const prim = ctx.alloc(v.POLY_FT4);
    const table = ctx.alloc([8]u32);
    const word0: *align(1) u32 = @ptrFromInt(zd.scratchpad);
    const word1: *align(1) u32 = @ptrFromInt(zd.scratchpad + 4);
    word0.* = psx.addr(prim);
    word1.* = psx.addr(&table[5]);
    const d = v.g.D_800F2280();
    d.vx = ctx.pick(i16, &.{ -32768, -32757, -11, 0, 11, 32756, 32767 });
    d.vy = ctx.pick(i16, &.{ -32768, -32, 0, 32, 32767 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const x = v.g.D_800F2280().vx;
    return if (x < -32000 or x > 32000) 1 else 0;
}
