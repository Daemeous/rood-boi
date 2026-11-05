//! zig_diff spec: vs_battle_setGeomOffset -> geometry_offset.setGeomOffset (D2: pc/src/room_geometry.c).
//! Input: two random words in the arena (the high halves are lost to the
//! 16-bit shift). The CTC2 writes (OFX, then OFY) are recording stubs, so
//! the register numbers, their order and the shifted values are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setGeomOffset));

pub const classes = [_][:0]const u8{"write"};

pub fn prepare(ctx: *zd.Ctx) Args {
    const offset: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    offset[0] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFF, 0x10000, 0xFFFFFFFF });
    offset[1] = ctx.pick(c_uint, &.{ 0, 1, 0xFFFF, 0x10000, 0xFFFFFFFF });
    return .{offset};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
