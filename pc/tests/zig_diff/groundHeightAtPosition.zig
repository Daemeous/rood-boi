//! zig_diff spec: func_800E42D4 -> pc.ground_height_at_position.groundHeightAtPosition (D2: pc/src/battle_movement.c).
//! Inputs: world positions (edges, negatives and values off the 128 grid, any
//! int): the shift by 7 is arithmetic, not a division. The stub of
//! func_800E42EC records the tile coordinates and returns a drawn value.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").groundHeightAtPosition));

pub const classes = [_][:0]const u8{ "non-negative", "negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const edges = [_]c_int{ 0, 1, -1, 127, 128, -127, -128, -129, 0x7FFFFFFF, -0x80000000, 1000, -1000 };
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] < 0 or args[1] < 0);
}
