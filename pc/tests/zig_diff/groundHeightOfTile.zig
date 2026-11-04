//! zig_diff spec: func_800E42E0 -> pc.ground_height_of_tile.groundHeightOfTile (D2: pc/src/battle_movement.c).
//! Inputs: a packed tile (x byte 0, z byte 2, noise in bytes 1 and 3). The stub
//! of func_800E42EC records the two coordinates and returns a drawn value,
//! which must come back unchanged.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").groundHeightOfTile));

pub const classes = [_][:0]const u8{"any"};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_uint, &.{ 0, 0xFFFFFFFF, 0x00FF00FF, 0xFF00FF00, 0x01020304 })};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
