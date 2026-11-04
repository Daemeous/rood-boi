//! zig_diff spec: func_800E3BC8 -> pc.record_step_tile.recordStepTile (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose tile (0x34) and target (0x168) share each axis,
//! or differ with the target ahead or behind (also at the byte extremes), and
//! random flags at 0x190 (only bits 0-21 are kept), so the equal/ahead/behind
//! choice of both axes and the overlapping shifted fields occur.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").recordStepTile));

pub const classes = [_][:0]const u8{ "reached", "x-moves", "z-moves", "both-move" };

fn axis(ctx: *zd.Ctx, tile: u8) u8 {
    return switch (ctx.range(u8, 0, 3)) {
        0 => tile,
        1 => tile +% 1,
        2 => tile -% 1,
        else => ctx.pick(u8, &.{ 0, 0xFF }),
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x34] = ctx.pick(u8, &.{ 0, 0xFF, 5 });
    block[0x36] = ctx.pick(u8, &.{ 0, 0xFF, 5 });
    block[0x168] = axis(ctx, block[0x34]);
    block[0x16a] = axis(ctx, block[0x36]);
    return .{block};
}

pub fn classify(args: Args) usize {
    const b = args[0];
    return @as(usize, @intFromBool(b[0x168] != b[0x34])) | @as(usize, @intFromBool(b[0x16a] != b[0x36])) << 1;
}
