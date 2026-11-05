//! zig_diff spec: func_8009723C -> draw_room_groups.drawRoomGroups (D2: pc/src/room_geometry.c).
//! Inputs: a room block in the arena (a group count, then 0x40-byte groups:
//! flags word, geometry offset at +0x1C, matrix at +0x20), the packet buffer
//! and ordering table the scratchpad words 0 and 1 point at, the facing mask
//! (word 4) and a mode. Every host hook is a stub steered by `plan`: the
//! native room (0, 1, 2), the free camera (with its facing, hidden groups
//! and the photo flag), whether the room is the scene's (which records the
//! drawn groups), the gameplay-walls pass (per-group verdict and cut
//! values) and pc_room_group_packets, which advances the packet cursor by
//! the group's geometry offset (0..13 words) or, for offsets 14 and 15,
//! lands on or past the buffer's limit. A count over 4096 draws one group
//! (the first returns the limit) after the pc_unimplemented stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_room_geometry");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawRoomGroups));

pub const classes = [_][:0]const u8{ "plain", "native", "free-camera", "record", "smart-walls", "limit", "over-count", "hidden-flags", "empty" };

const Plan = struct {
    native: c_int = 0,
    live: c_int = 0,
    facing: c_uint = 0,
    photo: c_int = 0,
    scene_match: bool = false,
    hidden: u32 = 0,
    walls: c_int = 0,
    walls_group: u32 = 0,
    cut_sign: u2 = 0,
};
var plan: Plan = .{};
var scenario: usize = 0;
var data_address: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    plan = .{};
    plan.native = if (scenario == 1) ctx.edge(c_int, &.{ 1, 2, 5 }) else if (ctx.chance(4)) ctx.edge(c_int, &.{ 0, 1, 2 }) else 0;
    plan.live = if (scenario == 2) 1 else if (scenario == 6) 0 else if (ctx.chance(6)) 1 else 0;
    plan.facing = ctx.pick(c_uint, &.{ 0x10000, 0, 0xFF, 0x1FF, 0xFFFF, 0x100FF });
    plan.photo = ctx.range(c_int, 0, 1);
    plan.scene_match = scenario == 3 or scenario == 4 or scenario == 6 or ctx.chance(6);
    plan.hidden = ctx.int(u32);
    plan.walls = if (scenario == 4) 1 else if (scenario != 6 and ctx.chance(6)) 1 else 0; // a count of 4096 must stop at the first group (the buffer holds 11)
    plan.walls_group = ctx.int(u32);
    plan.cut_sign = ctx.int(u2);

    const count: c_uint = switch (scenario) {
        8 => 0,
        6 => ctx.edge(c_uint, &.{ 4096, 4097 }),
        else => ctx.range(c_uint, 1, 10),
    };
    const data = ctx.allocBytes(4 + 12 * 0x40, 16);
    for (data[0 .. 4 + 12 * 0x40]) |*byte| byte.* = ctx.int(u8);
    data_address = @intFromPtr(data);
    for (v.g.drawn_groups()) |*seen| seen.* = ctx.int(u8); // the record's zeroing is visible
    std.mem.writeInt(c_uint, data[0..4], count, .little);
    const packets = ctx.allocBytes(0x10000 + 0x200, 16);
    const ot = ctx.allocBytes(0x40, 16);
    const scratch: [*]volatile u32 = @ptrFromInt(zd.scratchpad);
    scratch[0] = @intCast(@intFromPtr(packets));
    scratch[1] = @intCast(@intFromPtr(ot));
    const facing: u32 = if (scenario == 6) 0x10000 else ctx.edge(u32, &.{ 0x10000, 0x20000, 0, 0xFF, 0x0F, 0x100FF, ctx.int(u32), ctx.int(u32) & 0xFFFF });
    scratch[4] = facing;

    // The group that lands the cursor on the limit (14: past it).
    const limit_group: usize = if (scenario == 6) 0 else ctx.range(usize, 0, @max(count, 1) - 1);
    var i: usize = 0;
    while (i < @min(count, 11)) : (i += 1) {
        const group = data[4 + i * 0x40 ..][0..0x40];
        var flags = ctx.int(u32);
        if (scenario != 7 and !ctx.chance(5)) flags &= ~@as(u32, 0x1100);
        if (scenario == 6) flags = 0;
        if (ctx.chance(3)) flags = (flags & ~@as(u32, 0xFF)) | (facing & 0xFF); // the low mask bits are all in the group's flags
        std.mem.writeInt(u32, group[0..4], flags, .little);
        const k: u32 = if (i == limit_group and (scenario == 5 or scenario == 6)) ctx.edge(u32, &.{ 14, 15 }) else ctx.range(u32, 0, 13);
        std.mem.writeInt(u32, group[0x1c..0x20], 0x400 + k * 4, .little);
    }
    return .{ data, ctx.pick(c_uint, &.{ 0, 1, 2, 3 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return scenario;
}

pub const stubs = struct {
    pub fn pc_room_native_begin(ctx: *zd.Ctx, data: anytype, mode: anytype, first: anytype) c_int {
        _ = .{ ctx, data, mode, first };
        return plan.native;
    }
    pub fn pc_live_camera_active(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return plan.live;
    }
    pub fn pc_room_editor_facing(ctx: *zd.Ctx) c_uint {
        _ = ctx;
        return plan.facing;
    }
    pub fn pc_room_photo_whole(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return plan.photo;
    }
    pub fn pc_scene_room_data(ctx: *zd.Ctx) ?*const anyopaque {
        _ = ctx;
        return @ptrFromInt(if (plan.scene_match) data_address else data_address + 4);
    }
    pub fn pc_room_editor_hidden(ctx: *zd.Ctx, data: anytype, index: anytype) c_int {
        _ = .{ ctx, data };
        return @intCast((plan.hidden >> @intCast(index & 31)) & 1);
    }
    pub fn pc_gameplay_walls_frame(ctx: *zd.Ctx, data: anytype, facing: anytype, mode: anytype) c_int {
        _ = .{ ctx, data, facing, mode };
        return plan.walls;
    }
    pub fn pc_gameplay_walls_group(ctx: *zd.Ctx, index: anytype, kept: anytype, cut: anytype, cut_previous: anytype, ghost: anytype) c_int {
        _ = .{ ctx, kept };
        const values = [4]f32{ 0, 0.5, -1, 2 };
        cut.* = values[(plan.cut_sign +% @as(u2, @truncate(index))) & 3];
        cut_previous.* = values[(plan.cut_sign +% @as(u2, @truncate(index >> 1))) & 3];
        ghost.* = @intCast((index >> 2) & 1);
        return @intCast((plan.walls_group >> @intCast(index & 31)) & 1);
    }
    pub fn pc_room_group_packets(ctx: *zd.Ctx, geometry: anytype, packet: anytype, ot: anytype, gp: anytype, mode: anytype, limit: anytype) [*c]c_uint {
        _ = .{ ctx, ot, gp, mode };
        const k = (@intFromPtr(geometry) - data_address - 0x400) / 4;
        if (k == 15) return @constCast(limit);
        if (k == 14) return @constCast(limit) + 4;
        return packet + k;
    }
    pub fn pc_room_native_end(ctx: *zd.Ctx, packet: anytype, ot: anytype) [*c]c_uint {
        _ = .{ ctx, ot };
        return packet + 3;
    }
};
