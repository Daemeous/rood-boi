//! zig_diff spec: func_800D6CF0 -> func_800D6CF0.func_800D6CF0 (5BF94.c).
//! Inputs: D_800F569C an arena P-file whose block 2 is 40 random u16
//! offsets (above 0x7FFF too: they zero-extend), the index 0..37 (the C
//! reads entry index + 2), the table any int (narrowed to a byte).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6CF0));

pub const classes = [_][:0]const u8{ "first", "later", "high-offset" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const pf = ctx.alloc(v.D_800F569C_t);
    const block2: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(80, 16)));
    for (0..40) |i| block2[i] = ctx.pick(u16, &.{ 0, 2, 0x7FFF, 0x8000, 0xFFFF });
    psx.put(&pf.block2Data, @as(*u16, @ptrCast(block2)));
    v.g.D_800F569C().* = pf;
    return .{ ctx.alloc(v.func_800D6CF_t), ctx.pick(c_int, &.{ 0, 1, 255, 256, -1 }), ctx.range(c_int, 0, 37) };
}

pub fn classify(args: Args) usize {
    const block2: [*]u16 = @ptrCast(psx.get(v.g.D_800F569C().*.?.block2Data).?);
    if (block2[@intCast(args[2] + 2)] >= 0x8000) return 2;
    return if (args[2] == 0) 0 else 1;
}
