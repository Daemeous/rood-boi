//! zig_diff spec: func_800D821C -> pc.reset_actor_values.resetActorValues (D2: pc/src/actor_cleanup.c).
//! Inputs: an actor block whose byte 0x88 (the id) is 0, a small id, 0x40 or
//! 0xFF, with its neighbouring bytes random. The stub of func_80095B7C records
//! (id, value): a nonzero id makes the two calls 0 then 0x40, in that order;
//! id 0 makes none.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").resetActorValues));

pub const classes = [_][:0]const u8{ "id-zero", "id-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.allocBytes(0x100, 16);
    for (actor[0..0x100]) |*byte| byte.* = ctx.int(u8);
    actor[0x88] = ctx.edge(u8, &.{ 0, 0, 1, 5, 0x10, 0x40, 0x80, 0xFF });
    return .{actor};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0][0x88] != 0);
}
