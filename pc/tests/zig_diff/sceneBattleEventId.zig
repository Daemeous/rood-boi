//! zig_diff spec: pc_battle_event_id -> scene_submit.battleEventId (146C.c, scene_submit.inc).
//! Inputs: the event runner's state (0, nonzero), the source D_800F4BE0 and the event file
//! _evtFile at any value (the id is their low 16 bits, so negative files and high sources show).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleEventId));

pub const classes = [_][:0]const u8{ "none", "id" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C34().* = ctx.edge(u8, &.{ 0, 1, 4, 4, 255 });
    v.g.D_800F4BE0().* = ctx.pick(u16, &.{ 0, 1, 2, 0xF, 0x10, 0xFFFF });
    v.g._evtFile().* = ctx.pick(i16, &.{ 0, 1, 0xFFF, 0x1000, -1, -0x8000, 0x7FFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C34().* == 0) 0 else 1;
}
