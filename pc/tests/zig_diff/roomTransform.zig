//! zig_diff spec: func_800985AC -> pc.room_transform.roomTransform (D2: pc/src/boot_sdk.c).
//! Inputs: a position and an angle VECTOR and a result MATRIX in the arena. The
//! stub of pc_room_transform records its four arguments: the two vectors, the
//! scratchpad camera 0x1F800014 and the result.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_boot_sdk");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").roomTransform));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.alloc(c.VECTOR), ctx.alloc(c.VECTOR), ctx.alloc(c.MATRIX) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
