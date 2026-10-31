//! zig_diff spec: vs_battle_addTile -> add_tile.addTile (5BF94.c).
//! Inputs: a 16-byte primitive buffer in the arena that vs_scratch.unk0 points
//! at (so the C writes prim[0..3] and stores prim+4 back), and a `before` word
//! that is either a fresh arena word or the buffer's own first word, so the
//! read of *before and the write of prim[0] alias. Edge values cover the top
//! byte of *before (masked off by 0xFFFFFF) and the extremes of rgb0, xy and wh.
//! The C has no branches and calls nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").addTile));

pub const classes = [_][:0]const u8{ "hi-byte", "lo-byte", "alias-prim" };

/// The native address of the primitive buffer prepare built, for classify.
var prim_addr: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = v.g.vs_scratch();
    const prim: [*]u8 = ctx.allocBytes(16, 4);
    prim_addr = @intFromPtr(prim);
    const pa: *anyopaque = @ptrCast(prim);
    psx.put(&s.unk0, pa);

    const aliasPtr: *c_uint = @ptrCast(@alignCast(prim));
    const before: [*c]c_uint = if (ctx.chance(4)) aliasPtr else ctx.alloc(c_uint);
    before[0] = ctx.pick(u32, &.{ 0, 0xFFFFFFFF, 0xFF000000, 0x00FFFFFF, 0x80000000 });

    const edges = [_]c_int{ 0, -1, 1, 0x7FFFFFFF, -2147483648, 0x00FF00FF };
    const rgb0 = ctx.pick(c_int, &edges);
    const xy = ctx.pick(c_int, &edges);
    const wh = ctx.pick(c_int, &edges);
    return .{ before, rgb0, xy, wh };
}

pub fn classify(args: Args) usize {
    const before = args[0];
    if (@intFromPtr(before) == prim_addr) return 2;
    return if ((before[0] & 0xFF000000) != 0) 0 else 1;
}
