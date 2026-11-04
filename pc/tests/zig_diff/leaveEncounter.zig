//! zig_diff spec: func_800D82CC -> pc.leave_encounter.leaveEncounter (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose actor id (byte 0x88) is 0..31 and a room
//! (D_800F58BC) whose mask halfword (0x10) has the actor's bit set (or not:
//! ids 16..31 never are) and whose member count (0x12) is edge or random; the
//! actor list from vs_battle_actors[0]: a first node whose link word is 0 (no
//! second node), the address of a second node whose link is 0 (exactly two
//! actors) or nonzero (more). The stubs of func_800DBF00 and func_800D820C
//! record their arguments and the state at the call, so the order of the
//! stores around them shows.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").leaveEncounter));

pub const classes = [_][:0]const u8{ "not-engaged", "alone", "exactly-two", "more-than-two" };

var scenario: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const room = ctx.allocBytes(0x40, 16);
    for (room[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const id = if (ctx.chance(4)) ctx.range(u8, 16, 31) else ctx.range(u8, 0, 15);
    state[0x88] = id;
    var mask = ctx.int(u16);
    if (id < 16) {
        if (ctx.chance(5)) mask &= ~(@as(u16, 1) << @intCast(id)) else mask |= @as(u16, 1) << @intCast(id);
    }
    std.mem.writeInt(u16, room[0x10..][0..2], mask, .little);
    room[0x12] = ctx.edge(u8, &.{ 0, 1, 2, 0xFF, ctx.int(u8) });
    v.g.D_800F58BC().* = @intCast(@intFromPtr(room));
    const first = ctx.allocBytes(0x10, 16);
    const second = ctx.allocBytes(0x10, 16);
    for (first[0..0x10]) |*byte| byte.* = ctx.int(u8);
    for (second[0..0x10]) |*byte| byte.* = ctx.int(u8);
    scenario = ctx.range(usize, 0, 2);
    switch (scenario) {
        0 => std.mem.writeInt(u32, first[0..4], 0, .little),
        1 => {
            std.mem.writeInt(u32, first[0..4], @intCast(@intFromPtr(second)), .little);
            std.mem.writeInt(u32, second[0..4], 0, .little);
        },
        else => {
            std.mem.writeInt(u32, first[0..4], @intCast(@intFromPtr(second)), .little);
            std.mem.writeInt(u32, second[0..4], ctx.edge(u32, &.{ 1, 0x80000000, 0xFFFFFFFF, @as(u32, @intCast(@intFromPtr(first))) }), .little);
        },
    }
    v.g.vs_battle_actors()[0] = @ptrCast(first);
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0];
    const id = state[0x88];
    const room: [*]const u8 = @ptrFromInt(v.g.D_800F58BC().*);
    if (id >= 16 or (std.mem.readInt(u16, room[0x10..][0..2], .little) >> @intCast(id)) & 1 == 0) return 0;
    return 1 + scenario;
}
