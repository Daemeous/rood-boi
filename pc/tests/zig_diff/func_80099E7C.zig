//! zig_diff spec: func_80099E7C -> func_80099E7C.func_80099E7C (30DB0.c).
//! Inputs: a 512-byte block in the arena with about half its bytes set to 0x24
//! or 0x34, so both record shapes and both record tests occur at the walked
//! offsets. The count is -3..5 (negative and zero skip the loop; at most 5
//! records keeps every walk inside the block). The mode selector is 0, negative
//! or positive, and the two addends come from edges (0, +-1, 255, 256, -256,
//! the sign extremes) so byte stores wrap. The C calls nothing.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099E7C));

pub const classes = [_][:0]const u8{ "no-loop", "mode-zero", "mode-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(512, 16);
    for (0..512) |k| {
        if (ctx.chance(2)) buf[k] = ctx.edge(u8, &.{ 0x24, 0x34 });
    }
    const count = ctx.edge(c_int, &.{ -3, -1, 0, 1, 2, 3, 4, 5 });
    const mode = ctx.pick(c_int, &.{ 0, 1, -1 });
    const add3 = ctx.pick(c_int, &.{ 0, 1, -1, 255, 256, -256, 0x7fffffff, -0x80000000 });
    const add4 = ctx.pick(c_int, &.{ 0, 1, -1, 255, 256, -256, 0x7fffffff, -0x80000000 });
    return .{ @as(?*anyopaque, @ptrCast(buf)), count, mode, add3, add4 };
}

pub fn classify(args: Args) usize {
    if (args[1] <= 0) return 0;
    if (args[2] == 0) return 1;
    return 2;
}
