//! zig_diff spec: func_800E75EC -> pc.room_height_limit.roomHeightLimit (D2: pc/src/actor_cleanup.c).
//! Inputs: D_800F58BC zero a third of the time, else a random-filled record
//! (the word at 0x14 any int, negative included).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_cleanup");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").roomHeightLimit));

pub const classes = [_][:0]const u8{ "no-record", "record" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = ctx.allocBytes(0x40, 16);
    for (room[0..0x40]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(c_int, room[0x14..][0..4], ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 }), .little);
    v.g.D_800F58BC().* = if (ctx.chance(3)) 0 else @intCast(@intFromPtr(room));
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(v.g.D_800F58BC().* != 0);
}
