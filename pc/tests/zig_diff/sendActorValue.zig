//! zig_diff spec: func_800D820C -> pc.send_actor_value.sendActorValue (D2: pc/src/battle_helpers.c).
//! Inputs: an actor id (0, small ids, any int) and a value (edges or any int).
//! The stub of func_80095B7C records its two arguments, so a swapped or
//! dropped argument shows, and id 0 must make no call at all.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").sendActorValue));

pub const classes = [_][:0]const u8{ "id-zero", "id-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{
        if (ctx.chance(4)) 0 else ctx.pick(c_int, &.{ 1, 2, 16, -1, 0x7FFFFFFF, -0x80000000 }),
        ctx.pick(c_int, &.{ 0, 1, -1, 0x80, 0x7FFFFFFF, -0x80000000 }),
    };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] != 0);
}
