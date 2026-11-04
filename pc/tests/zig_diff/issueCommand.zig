//! zig_diff spec: func_800DEEFC -> pc.issue_command.issueCommand (D2: pc/src/battle_movement.c).
//! Inputs: an actor state block (0x400 bytes, random) whose command block (word
//! at 0x54), model (0x58) and candidate record (0x22C) are arena blocks;
//! an action 0..17 (every prong), 18 and -1 (the default); fields chosen at
//! the edges of the tests: the turn limit and the heading offset of action 4
//! (0, 0x7FF/0x800 and 0xFFF, rates 0, 1, large), the gait word (every flag
//! of both gaits, running byte 1 or not, speeds 0..255), and for the moves
//! (actions 5-7) a tile in 0..15 with the position within and just outside
//! it, the nav tables of scratchpad 0x3BC-0x3F8 (floor grid, occupancy bitmap
//! and its flag, direction bytes -1 or 0..7), the saved slide tile (0x9C)
//! equal to the tile or not, and a mode in which nothing moves and the slide
//! target is the position (the zero-slide return). rcos/rsin return small
//! values (so the step stays near the tile), func_800E4690 and func_800E42E0
//! small/random ones, and ratan2 and func_800E42D4 any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").issueCommand));

pub const classes = [_][:0]const u8{
    "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
    "9",  "10", "11", "12", "13", "14", "15", "16", "17",
    "default",
};

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

var force_block = false;
var small_heights = false;

fn small(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u3)) {
        0 => 0,
        1 => 4096,
        2 => -4096,
        else => @as(c_int, ctx.value(i16)) >> 3,
    };
}

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx);
    }
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx);
    }
    pub fn func_800E4690(ctx: *zd.Ctx, tile: anytype, dir: anytype) c_int {
        _ = tile;
        _ = dir;
        return if (force_block) 1 else ctx.value(u1);
    }
    pub fn func_800E42E0(ctx: *zd.Ctx, tile: anytype) c_int {
        _ = tile;
        if (small_heights) return @as(c_int, ctx.value(u2)) - 1;
        return ctx.value(i8);
    }
};

fn setWord(base: [*]u8, offset: usize, value: u32) void {
    @as(*align(1) u32, @ptrCast(base + offset)).* = value;
}

fn setHalf(base: [*]u8, offset: usize, value: u16) void {
    @as(*align(1) u16, @ptrCast(base + offset)).* = value;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    force_block = false;
    small_heights = false;
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    const command = ctx.allocBytes(0x40, 16);
    for (command[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const model = ctx.allocBytes(0x80, 16);
    for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
    const candidate: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    for (candidate[0..4]) |*word| word.* = ctx.int(u32);
    for (0..16) |i| {
        const slot_model = ctx.allocBytes(0x80, 16);
        for (slot_model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        v.g.D_800F4538()[i] = @ptrCast(slot_model);
    }
    setWord(state, 0x54, @intCast(@intFromPtr(command)));
    setWord(state, 0x58, @intCast(@intFromPtr(model)));
    setWord(state, 0x22c, @intCast(@intFromPtr(candidate)));

    const action = ctx.edge(c_int, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, -1, 5, 6, 7, 4, 14 });

    // Action 4: the heading offset and the limits at their edges.
    const turn: i16 = ctx.pick(i16, &.{ 0, 0x31F, 0x320, -0x31F, -0x320, -0x321, 0x321 });
    const heading: i16 = ctx.int(i16);
    @as(*align(1) i16, @ptrCast(model + 0x14)).* = turn;
    @as(*align(1) i16, @ptrCast(model + 0x26)).* = heading;
    const offset = ctx.pick(u16, &.{ 0, 1, 0x7FF, 0x800, 0xFFF });
    setHalf(state, 0x18a, (@as(u16, @bitCast(turn +% heading)) +% offset) & 0xFFF | (ctx.int(u16) & 0xF000));
    setHalf(state, 0x13a, ctx.pick(u16, &.{ 0, 1, 2, 0x100, 0x7FF, 0x800, 0xFFF, 0x1000 }));
    state[0x2a] = ctx.edge(u8, &.{ 0, 1, 2, 0xFF });

    // The tile the moves start from, and the nav tables around it.
    const tile_x = ctx.range(u8, 0, 15);
    const tile_z = ctx.range(u8, 0, 15);
    setWord(state, 0x34, @as(u32, tile_x) | (@as(u32, tile_z) << 16) | (ctx.int(u32) & 0xFF00FF00));
    var size = ctx.edge(u16, &.{ 0, 0x20, 0x40, 0x7F, 0x80, 0x92 });
    var x: u16 = @as(u16, tile_x) * 128 +% ctx.edge(u16, &.{ 0, 63, 64, 127, 0xFFC0, 0x20, 0x90, 200 });
    var z: u16 = @as(u16, tile_z) * 128 +% ctx.edge(u16, &.{ 0, 63, 64, 127, 0xFFC0, 0x20, 0x90, 200 });
    var gait = ctx.int(u32);
    gait = (gait & ~@as(u32, 0x8484)) | (ctx.int(u32) & 0x8484 & ctx.int(u32));
    for (0x3d0..0x3f8) |i| @as(*u8, @ptrFromInt(0x1F800000 + i)).* = ctx.edge(u8, &.{ 0xFF, 0, 1, 2, 3, 4, 5, 6, 7 });
    @as(*u8, @ptrFromInt(0x1F8003DC)).* = ctx.range(u8, 0, 8);
    const floors = ctx.allocBytes(0x800, 16);
    for (floors[0..0x800]) |*byte| byte.* = ctx.int(u8);
    const bitmap = ctx.allocBytes(0x100, 16);
    for (bitmap[0..0x100]) |*byte| byte.* = ctx.int(u8);
    @as(*u32, @ptrFromInt(0x1F8003BC)).* = @intCast(@intFromPtr(floors + 0x400));
    @as(*u32, @ptrFromInt(0x1F8003C4)).* = @intCast(@intFromPtr(bitmap));
    if (ctx.chance(2)) @as(*u32, @ptrFromInt(0x1F8003F4)).* = 0;
    if (ctx.chance(3)) setWord(state, 0x9c, @as(u32, tile_x) | (@as(u32, tile_z) << 16) | (ctx.int(u32) & 0xFF00FF00));

    // A move that goes nowhere and whose slide target is the position itself.
    if ((action == 5 or action == 6 or action == 7) and ctx.chance(5)) {
        size = 0x20;
        x = @as(u16, tile_x) * 128 + 0x20;
        z = @as(u16, tile_z) * 128 + 0x20;
        gait = 0x42;
        state[1] = ctx.edge(u8, &.{ 0, 1 });
        @as(*u8, @ptrFromInt(0x1F8003E5)).* = 0;
        @as(*u32, @ptrFromInt(0x1F8003F4)).* = 0;
        setWord(state, 0x9c, 0);
        force_block = true;
    }
    setHalf(state, 0x92, size);
    setHalf(state, 0x4c, x);
    setHalf(state, 0x50, z);
    setWord(state, 0x224, gait);

    // Tiles to walk to: a zero byte 1 (the one test of action 14) half the time.
    if (ctx.chance(2)) state[0x171] = 0;
    setHalf(state, 0xb0, ctx.pick(u16, &.{ 0, 1, 0x80, 0xFF }));
    // Action 14: heights in -1..2 against a threshold in 0..4, so the rise
    // equals the threshold often; and the command's word at 4 zero some of the time.
    if (action == 14 and ctx.chance(2)) {
        small_heights = true;
        setHalf(state, 0xb0, ctx.range(u16, 0, 4));
    }
    if (ctx.chance(3)) setWord(command, 4, 0);
    setHalf(state, 0x15e, ctx.edge(u16, &.{ 0, 8, 0x30, 0x10, 0xC0, 0x40, 0x38, 0xFFFF }));
    state[1] = if (force_block) state[1] else ctx.edge(u8, &.{ 0, 1 });
    // Slot flags of action 15: bits 3-4 select the branch.
    candidate[1] = ctx.int(u32);
    return .{ state, action };
}

pub fn classify(args: Args) usize {
    return if (args[1] >= 0 and args[1] <= 17) @intCast(args[1]) else 18;
}
