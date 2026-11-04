//! zig_diff spec: func_800E5154 -> pc.route_target_state.routeTargetState (D2: pc/src/actor_update.c).
//! Inputs as freeActorRoute's, with the route record (guest word at the
//! state's +0x18C) holding a pointer at +0x64 to an arena tile word and the
//! target word at +0x168: equal in the masked bits (0xFF00FF) with the other
//! bits random, different in a masked byte (each of the three), or random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_update");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").routeTargetState));

pub const classes = [_][:0]const u8{ "bad-id", "no-grid", "no-state", "no-route", "on-target", "elsewhere" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: c_uint = if (ctx.chance(6)) ctx.edge(c_uint, &.{ 17, 18, 40, 255 }) else ctx.range(c_uint, 0, 16);
    const table = v.g.D_800F5878();
    for (0..256) |slot| table[slot] = null;
    if (!ctx.chance(6)) {
        const state = ctx.allocBytes(0x200, 16);
        for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
        var route_address: u32 = 0;
        if (!ctx.chance(4)) {
            const route = ctx.allocBytes(0x200, 16);
            for (route[0..0x200]) |*byte| byte.* = ctx.int(u8);
            const tile = ctx.allocBytes(16, 16);
            for (tile[0..16]) |*byte| byte.* = ctx.int(u8);
            const tile_word = std.mem.readInt(u32, tile[0..4], .little);
            const target = switch (ctx.range(u8, 0, 4)) {
                0 => tile_word, // the same word
                1 => (tile_word & 0x00FF00FF) | (ctx.int(u32) & 0xFF00FF00), // masked bits equal
                2 => tile_word ^ 0x00000001,
                3 => tile_word ^ 0x00010000,
                else => tile_word ^ 0x00FF0000 ^ 0x000000FF,
            };
            std.mem.writeInt(u32, route[0x64..][0..4], @intCast(@intFromPtr(tile)), .little);
            std.mem.writeInt(u32, route[0x168..][0..4], target, .little);
            route_address = @intCast(@intFromPtr(route));
        }
        std.mem.writeInt(u32, state[0x18c..][0..4], route_address, .little);
        table[id] = @ptrCast(state);
    }
    v.g.D_800F58B8().* = if (ctx.chance(5)) 0 else ctx.edge(u32, &.{ 1, 0x80100000, 0xFFFFFFFF });
    return .{id};
}

pub fn classify(args: Args) usize {
    if (args[0] >= 17) return 0;
    if (v.g.D_800F58B8().* == 0) return 1;
    const state: [*]const u8 = @ptrCast(v.g.D_800F5878()[args[0]] orelse return 2);
    const route_address = std.mem.readInt(u32, state[0x18c..][0..4], .little);
    if (route_address == 0) return 3;
    const route: [*]const u8 = @ptrFromInt(route_address);
    const tile: *align(1) const u32 = @ptrFromInt(std.mem.readInt(u32, route[0x64..][0..4], .little));
    const target = std.mem.readInt(u32, route[0x168..][0..4], .little);
    return if (tile.* & 0xFF00FF == target & 0xFF00FF) 4 else 5;
}
