//! zig_diff spec: func_800DB8AC -> pc.can_engage_target.canEngageTarget (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the force halfword (0x8E) and the block
//! halfword (0x98) zero or set, the actor id (0x88) 0..15 and the relation
//! words (0x1A4 + target * 4) with bit 0 set or clear; a candidate record
//! whose target (word 3, low nibble) is 0..15; the room (D_800F58BC) with the
//! counter (0x0D), the members (0x12) and the threshold word (0x24) at, below
//! and above each other and the per-actor mask (0x10) with the actor's bit set
//! or clear. The stub of func_800DBCB4 records its arguments and must be
//! called first.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").canEngageTarget));

pub const classes = [_][:0]const u8{ "forced", "blocked", "no-relation-bit", "below-threshold", "mask-bit", "members-full", "members-room" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(u16, state[0x8e..][0..2], ctx.edge(u16, &.{ 0, 0, 0, 1, 0x100 }), .little);
    std.mem.writeInt(u16, state[0x98..][0..2], ctx.edge(u16, &.{ 0, 0, 1, 0x8000 }), .little);
    state[0x88] = ctx.range(u8, 0, 15);
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    const target = candidate[12] & 0xf;
    if (ctx.chance(4)) state[0x1a4 + @as(usize, target) * 4] &= 0xFE else state[0x1a4 + @as(usize, target) * 4] |= 1;
    const room = ctx.allocBytes(0x40, 16);
    for (room[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const count = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 0xFF });
    room[0xd] = count;
    room[0x12] = ctx.edge(u8, &.{ count, count -% 1, count +% 1, 0, 0xFF });
    std.mem.writeInt(u32, room[0x24..][0..4], ctx.edge(u32, &.{ count, count -% 1, count +% 1, 0, 0xFFFFFFFF }), .little);
    var mask = ctx.int(u16);
    if (ctx.chance(2)) mask |= @as(u16, 1) << @intCast(state[0x88]) else mask &= ~(@as(u16, 1) << @intCast(state[0x88]));
    std.mem.writeInt(u16, room[0x10..][0..2], mask, .little);
    v.g.D_800F58BC().* = @intCast(@intFromPtr(room));
    return .{ state, candidate };
}

pub fn classify(args: Args) usize {
    const state = args[0];
    const target = args[1][12] & 0xf;
    const room: [*]const u8 = @ptrFromInt(v.g.D_800F58BC().*);
    if (std.mem.readInt(u16, state[0x8e..][0..2], .little) != 0) return 0;
    if (std.mem.readInt(u16, state[0x98..][0..2], .little) != 0) return 1;
    if (state[0x1a4 + @as(usize, target) * 4] & 1 == 0) return 2;
    if (room[0xd] < std.mem.readInt(u32, room[0x24..][0..4], .little)) return 3;
    if (std.mem.readInt(u16, room[0x10..][0..2], .little) >> @intCast(state[0x88]) & 1 != 0) return 4;
    return if (room[0xd] < room[0x12]) 5 else 6;
}
