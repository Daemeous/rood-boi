//! zig_diff spec: pc_battle_event_running -> scene_submit.battleEventRunning (146C.c, scene_submit.inc).
//! Inputs: the event runner's state D_800F4C34 (0, 1..4, 255 and any byte).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleEventRunning));

pub const classes = [_][:0]const u8{ "idle", "running" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C34().* = ctx.pick(u8, &.{ 0, 0, 1, 2, 3, 4, 255 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C34().* == 0) 0 else 1;
}
