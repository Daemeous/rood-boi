//! zig_diff spec: vs_battle_getGeomOffset -> geometry_offset.getGeomOffset (D2: pc/src/room_geometry.c).
//! Input: a two-word output block in the arena. The two CFC2 reads (OFX,
//! then OFY) are recording stubs returning a different value each call, so
//! the register numbers, their order, the 16-bit shift and the word each
//! lands in are all compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getGeomOffset));

pub const classes = [_][:0]const u8{"read"};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{@ptrCast(@alignCast(ctx.allocBytes(8, 16)))};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
