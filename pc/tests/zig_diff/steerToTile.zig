//! zig_diff spec: func_800E4B2C -> steer_to_tile.steerToTile (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes where the gate byte 0x19B is
//! sometimes nonzero (any nonzero value, the sign bit included); the packed
//! target tile (0x168) is the actor's own tile (0x34) in bits 0-7 and 16-23
//! a third of the time, with the bits 8-15 and 24-31 random (the comparison
//! ignores them); the heading is any int. The callees are recording stubs, so
//! the stored centre (0x17C, 0x180) and the arguments of each call show.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").steerToTile));

pub const classes = [_][:0]const u8{ "gate-held", "on-target", "head-to-target" };

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    state[0x19b] = if (ctx.chance(4)) ctx.edge(u8, &.{ 1, 2, 0x80, 0xff }) else 0;
    if (ctx.chance(3)) word(state, 0x34).* = (word(state, 0x168).* & 0xff00ff) | (ctx.int(u32) & 0xff00ff00);
    const angle = ctx.pick(c_int, &.{ 0, 1, -1, 0x7ff, 0x800, 0xfff, 0x1000, 0x7FFFFFFF, -0x80000000 });
    return .{ state, angle };
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (state[0x19b] != 0) return 0;
    return if (word(state, 0x168).* & 0xff00ff == word(state, 0x34).* & 0xff00ff) 1 else 2;
}
