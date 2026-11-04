//! zig_diff spec: func_800E3D5C -> pc.axis_passability.axisPassability (D2: pc/src/battle_movement.c).
//! Inputs: a state pointer and a test function that logs each call's direction
//! in an arena array (so an order or direction difference shows in the arena
//! compare) and returns the prepared result for that direction: 0 or 1 (the
//! real tests), or any int (the combining shifts are plain int shifts).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").axisPassability));

pub const classes = [_][:0]const u8{ "bits", "any-values" };

var results: [4]c_int = undefined;
var log: [*]c_int = undefined;
var logged: usize = 0;

fn test_(state: ?*anyopaque, direction: c_int) callconv(.c) c_int {
    _ = state;
    if (direction == 0) logged = 0; // the first call of a run
    log[logged] = direction;
    logged += 1;
    return results[@intCast(@as(c_uint, @bitCast(direction)) >> 1 & 3)];
}

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    for (log[0..16]) |*word| word.* = 0;
    logged = 0;
    const plain = ctx.chance(2);
    for (&results) |*r| r.* = if (plain) ctx.range(c_int, 0, 1) else ctx.pick(c_int, &.{ 0, 1, -1, 2, 0x7FFFFFFF, -0x80000000 });
    return .{ ctx.allocBytes(16, 16), &test_ };
}

pub fn classify(args: Args) usize {
    _ = args;
    for (results) |r| if (r != 0 and r != 1) return 1;
    return 0;
}
