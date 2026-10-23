//! zig_diff spec: func_8009651C -> func_8009651C.func_8009651C (2842C.c).
//! Inputs: a random position (vx and vy edged at the i16 extremes and at the
//! small offsets, so the dx/dy sums wrap), and a direction drawn from the eight
//! mapped values, the unmapped ones (0, 3, 7, 11, -1, 12) and random values.
//! The scratchpad's primitive pointer is an arena POLY_FT4 and the ordering
//! table pointer an arena block (ot is unk4 + 16, so AddPrim gets the block).
//! AddPrim is a recording stub that may overwrite the ordering word and the
//! primitive's tag, as the real AddPrim links the primitive into the table.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009651C));

pub const classes = [_][:0]const u8{ "dir4", "dir6", "dir2", "dir10", "dir8", "dir9", "dir1", "dir5", "other" };

fn classOf(d: c_int) usize {
    return switch (d) {
        4 => 0,
        6 => 1,
        2 => 2,
        10 => 3,
        8 => 4,
        9 => 5,
        1 => 6,
        5 => 7,
        else => 8,
    };
}

pub const stubs = struct {
    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        if (ctx.value(u1) == 1) {
            if (ot) |o| {
                const w: *align(1) u32 = @ptrCast(o);
                w.* = ctx.value(u32);
            }
        }
        if (ctx.value(u1) == 1) {
            if (p) |q| {
                const w: *align(1) u32 = @ptrCast(q);
                w.* = ctx.value(u32);
            }
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const pos = ctx.alloc(v.SVECTOR);
    pos.vx = ctx.pick(i16, &.{ -32768, 32767, -8, 0, 7 });
    pos.vy = ctx.pick(i16, &.{ -32768, 32767, -11, 0, 10 });
    const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    psx.put(&scratch.unk0, ctx.alloc(v.POLY_FT4));
    const ot = ctx.allocBytes(64, 16);
    psx.put(&scratch.unk4, ot + 16);
    const dir = ctx.pick(c_int, &.{ 4, 6, 2, 10, 8, 9, 1, 5, 0, 3, 7, 11, -1, 12 });
    return .{ pos, dir };
}

pub fn classify(args: Args) usize {
    return classOf(args[1]);
}
