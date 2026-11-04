//! zig_diff spec: func_800B4B14 -> pc.shadow_actor.shadowActor (D2: pc/src/actor_shadow.c).
//! Inputs: an actor block in the arena (or null: it is only passed on). The
//! stub of pc_actor_shadow records (actor, scratchpad base).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").shadowActor));

pub const classes = [_][:0]const u8{ "null-actor", "actor" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(8)) return .{null};
    const actor = ctx.allocBytes(0x1900, 16);
    for (actor[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    return .{@ptrCast(actor)};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] != null);
}
