//! zig_diff spec: func_800D836C -> pc.join_encounter.joinEncounter (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the engagement timer (0x8E), the block (0x98)
//! and the recent-leave timer (0x9A) zero or set, the actor id (0x88) 0..31;
//! the room (D_800F58BC) with its counter (0x0D) at, below and above its
//! member count (0x12, also 0xFF so the increment wraps), the mask (0x10),
//! the engaged list head (0x1C) and tail (0x20); D_800F58E0 zero or set.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").joinEncounter));

pub const classes = [_][:0]const u8{ "already-engaged", "full", "blocked-by-state", "blocked-by-global", "joined-fresh", "joined-after-leave", "joined-empty-list" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(u16, state[0x8e..][0..2], ctx.edge(u16, &.{ 0, 0, 0, 1, 0x100 }), .little);
    std.mem.writeInt(u16, state[0x98..][0..2], ctx.edge(u16, &.{ 0, 0, 0, 1, 0x8000 }), .little);
    std.mem.writeInt(u16, state[0x9a..][0..2], ctx.edge(u16, &.{ 0, 0, 1, 90, 0x100 }), .little);
    state[0x88] = if (ctx.chance(6)) ctx.range(u8, 16, 31) else ctx.range(u8, 0, 15);
    const room = ctx.allocBytes(0x40, 16);
    for (room[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const members = ctx.edge(u8, &.{ 0, 1, 2, 5, 0xFE, 0xFF });
    room[0x12] = members;
    room[0xd] = ctx.edge(u8, &.{ members, members -% 1, members +% 1, 0, 0xFF });
    std.mem.writeInt(u32, room[0x1c..][0..4], ctx.edge(u32, &.{ 0, 0, 0x1000, 0xFFFFFFFF, ctx.int(u32) }), .little);
    v.g.D_800F58BC().* = @intCast(@intFromPtr(room));
    v.g.D_800F58E0().* = ctx.edge(c_uint, &.{ 0, 0, 0, 1, 0x10000, 0x80000000 });
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0];
    const room: [*]const u8 = @ptrFromInt(v.g.D_800F58BC().*);
    if (std.mem.readInt(u16, state[0x8e..][0..2], .little) != 0) return 0;
    if (room[0xd] < room[0x12]) return 1;
    if (std.mem.readInt(u16, state[0x98..][0..2], .little) != 0) return 2;
    if (v.g.D_800F58E0().* != 0) return 3;
    if (std.mem.readInt(u32, room[0x1c..][0..4], .little) == 0) return 6;
    if (std.mem.readInt(u16, state[0x9a..][0..2], .little) != 0) return 5;
    return 4;
}
