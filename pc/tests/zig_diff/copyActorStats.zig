//! zig_diff spec: func_800DC3E8 -> pc.copy_actor_stats.copyActorStats (D2: pc/src/battle_helpers.c).
//! Inputs: two random-filled 0x1000-byte actor blocks (source and
//! destination), or the same block as both. The words at 0x18, 0x1C, 0x30
//! and 0x948 are copied directly; the stub of vs_main_memcpy records
//! (destination + 0x398, source + 0x398, 0x528) and copies nothing, so the
//! block stays as it was.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").copyActorStats));

pub const classes = [_][:0]const u8{ "distinct", "same-block" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const src = ctx.allocBytes(0x1000, 16);
    for (src[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(6)) return .{ src, src };
    const dst = ctx.allocBytes(0x1000, 16);
    for (dst[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    return .{ src, dst };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] == args[1]);
}
