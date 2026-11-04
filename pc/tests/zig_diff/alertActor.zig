//! zig_diff spec: func_800E4C64 -> alert_actor.alertActor (D2: pc/src/battle_movement.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots empty a
//! fourth of the time, else a random-filled block; an actor id 0 a sixth of
//! the time, else 1..16. The callee func_800E4C8C is a recording stub whose
//! argument is the state, so the 30 stored at 0x13F is seen in its hash.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").alertActor));

pub const classes = [_][:0]const u8{ "id-zero", "empty-slot", "alerted" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(4)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x200, 16);
            for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    return .{if (ctx.chance(6)) 0 else ctx.range(c_int, 1, 16)};
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    return if (v.g.D_800F5878()[@intCast(args[0])] == null) 1 else 2;
}
