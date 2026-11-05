//! zig_diff spec: func_80098648 -> pc.room_transform.scaledRoomTransform (D2: pc/src/boot_sdk.c).
//! Inputs: a position and an angle VECTOR and a result MATRIX in the arena, and
//! the far-clip word at scratchpad 0x1F800064 (zero, small, negative or any int,
//! with the neighbouring words random so a wrong address shows). The stub of
//! pc_scaled_room_transform records its five arguments.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_boot_sdk");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").scaledRoomTransform));

pub const classes = [_][:0]const u8{ "zero", "other" };

var far_clip: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]volatile c_int = @ptrFromInt(0x1f800000);
    scratch[0x60 / 4] = ctx.int(c_int);
    far_clip = ctx.pick(c_int, &.{ 0, 1, -1, 0x1000, 0x7fffffff, -0x80000000, 0x7ff });
    scratch[0x64 / 4] = far_clip;
    scratch[0x68 / 4] = ctx.int(c_int);
    return .{ ctx.alloc(c.VECTOR), ctx.alloc(c.VECTOR), ctx.alloc(c.MATRIX) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(far_clip != 0);
}
