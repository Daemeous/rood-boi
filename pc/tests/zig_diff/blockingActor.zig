//! zig_diff spec: func_800D98E8 -> pc.blocking_actor.blockingActor (D2: pc/src/battle_movement.c).
//! Inputs: a direction 0..7 whose cache bit (scratchpad 0x3D2) is set (the
//! cached byte at 0x3D4 + dir is returned) or clear; the packed direction
//! (0x3EC + dir, bits 0-1 and 4-5 the signed steps, the other bits random); the
//! actor's position, radius (0xB3) and tile (0x34) with the neighbour index
//! in 0..10; the lookup byte of the neighbour (0xFF or 0..7) against the
//! occupancy bitmap (0x3C4, when word 0x3F4 is set) or func_800E4690's answer
//! (the stub), which pick the height from the state (0x4E) or from
//! func_800E42D4 (the stub). The actor list holds actors that overlap (at the
//! reach, the height reach, and inside), miss on one axis, lie behind the
//! direction, or are the actor itself (placed on the point: it must be
//! skipped); an actor's AI record gives its reach and size, its position
//! comes from it or from its model.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").blockingActor));

pub const classes = [_][:0]const u8{ "cached", "no-hit", "hit-first", "hit-later", "behind-direction-skipped" };

var stage: usize = 0;
var open_stub: c_int = 0;
var height_stub: c_int = 0;

pub const stubs = struct {
    pub fn func_800E4690(ctx: *zd.Ctx, tile: anytype, lookup: anytype) c_int {
        _ = ctx;
        _ = tile;
        _ = lookup;
        return open_stub;
    }
    pub fn func_800E42D4(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = ctx;
        _ = x;
        _ = z;
        return height_stub;
    }
};

fn put16(p: [*]u8, off: usize, value: u16) void {
    std.mem.writeInt(u16, p[off..][0..2], value, .little);
}

fn put32(p: [*]u8, off: usize, value: u32) void {
    std.mem.writeInt(u32, p[off..][0..4], value, .little);
}

fn randomBlock(ctx: *zd.Ctx, size: usize) [*]u8 {
    const block = ctx.allocBytes(size, 16);
    for (block[0..size]) |*byte| byte.* = ctx.int(u8);
    return block;
}

fn signed2(bits: u8) i32 {
    return switch (bits & 3) {
        0 => 0,
        1 => 1,
        2 => -2,
        else => -1,
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]u8 = @ptrFromInt(zd.scratchpad);
    stage = ctx.range(usize, 0, 4);
    const dir: u8 = ctx.range(u8, 0, 7);
    scratch[0x3d2] = ctx.int(u8);
    if (stage == 0) scratch[0x3d2] |= @as(u8, 1) << @intCast(dir) else scratch[0x3d2] &= ~(@as(u8, 1) << @intCast(dir));
    scratch[0x3d4 + @as(usize, dir)] = ctx.int(u8);
    // The packed direction: some step must be nonzero.
    var packed_direction: u8 = ctx.int(u8);
    if (packed_direction & 0x33 == 0) packed_direction |= ctx.edge(u8, &.{ 1, 2, 3, 0x10, 0x20, 0x30, 0x11 });
    scratch[0x3ec + @as(usize, dir)] = packed_direction;
    const dx = signed2(packed_direction);
    const dz = signed2(packed_direction >> 4);

    const state = randomBlock(ctx, 0x200);
    const tile_x = ctx.range(u8, 1, 120);
    const tile_z = ctx.range(u8, 1, 120);
    put32(state, 0x34, @as(u32, tile_x) | (@as(u32, ctx.int(u8)) << 8) | (@as(u32, tile_z) << 16) | (@as(u32, ctx.int(u8)) << 24));
    const radius: i32 = ctx.edge(i32, &.{ 0, 1, 0x20, 0x40, ctx.range(i32, 0, 0x40) });
    state[0xb3] = @intCast(radius);
    const pos_x: i32 = @as(i32, tile_x) * 128 + ctx.edge(i32, &.{ 0, 127, 64, ctx.range(i32, 0, 127) });
    const pos_z: i32 = @as(i32, tile_z) * 128 + ctx.edge(i32, &.{ 0, 127, 64, ctx.range(i32, 0, 127) });
    put16(state, 0x4c, @intCast(pos_x));
    put16(state, 0x50, @intCast(pos_z));
    const x: i32 = pos_x + radius * dx;
    const z: i32 = pos_z + radius * dz;
    const neighbour: usize = @intCast(((z >> 7) - tile_z + 1) * 4 + ((x >> 7) - @as(i32, tile_x) + 1));
    for (0..16) |i| scratch[0x3e0 + i] = ctx.edge(u8, &.{ 0xFF, 0, 1, 2, 3, 7, 0x80, 0xFF });
    scratch[0x3e0 + neighbour] = ctx.edge(u8, &.{ 0xFF, 0, 1, 2, 3, 7 });
    const lookup: i8 = @bitCast(scratch[0x3e0 + neighbour]);
    const width = ctx.range(u8, 1, 12);
    scratch[0x3dc] = width;
    put32(scratch, 0x3f4, if (ctx.chance(2)) 0 else ctx.edge(u32, &.{ 1, 0x80000000, 0xFFFFFFFF }));
    const bitmap = randomBlock(ctx, 0x1000);
    put32(scratch, 0x3c4, @intCast(@intFromPtr(bitmap)));
    open_stub = ctx.edge(c_int, &.{ 0, 1, 2, -1 });
    height_stub = ctx.pick(c_int, &.{ 0, 100, -100, 0x1000 });
    put16(state, 0x4e, ctx.pick(u16, &.{ 0, 50, 0xFF00 }));
    var open = true;
    if (lookup >= 0) {
        if (std.mem.readInt(u32, scratch[0x3f4..][0..4], .little) != 0) {
            open = (bitmap[@as(usize, width) * tile_z + tile_x] >> @intCast(lookup)) & 1 != 0;
        } else {
            open = open_stub != 0;
        }
    }
    const height: i32 = if (open) @as(i16, @bitCast(std.mem.readInt(u16, state[0x4e..][0..2], .little))) else height_stub;
    var self_size = ctx.range(u16, 0, 0x30);
    if (ctx.chance(3)) self_size = 0;
    put16(state, 0x94, self_size);
    const self_height: u16 = ctx.range(u16, 0, 0x60);
    put16(state, 0xb8, self_height);
    state[0x88] = ctx.range(u8, 0, 16);

    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = randomBlock(ctx, 0x80);
        if (ctx.chance(2)) model[0xa] &= 0xE0;
        table[i] = @ptrCast(model);
        put32(scratch, 0x37c + 4 * i, @intCast(@intFromPtr(randomBlock(ctx, 0x200))));
    }

    // The actors: the "target" one overlaps (stage 2: first, stage 3: later);
    // stage 4's first actor overlaps but lies behind the direction.
    const count = ctx.range(usize, 1, 4);
    const target = switch (stage) {
        2 => 0,
        3 => if (count > 1) ctx.range(usize, 1, count - 1) else 0,
        else => count,
    };
    var first: ?[*]u8 = null;
    var previous: ?[*]u8 = null;
    for (0..count) |i| {
        var id = ctx.range(usize, 0, 16);
        const behind = stage == 4 and i == 0;
        const is_target = i == target;
        if ((is_target or behind) and id == state[0x88]) id = (id + 1) % 17;
        if (!is_target and !behind and ctx.chance(4)) id = state[0x88];
        const node = randomBlock(ctx, 0x10);
        put32(node, 0, 0);
        put32(node, 4, @intCast(id));
        if (previous) |p| put32(p, 0, @intCast(@intFromPtr(node))) else first = node;
        previous = node;
        const model: [*]u8 = @ptrCast(table[id].?);
        const other: [*]u8 = @ptrFromInt(std.mem.readInt(u32, scratch[0x37c + 4 * id ..][0..4], .little));
        const uses_other = id != 0 and model[0xa] & 0x1f != 0;
        const holder: [*]u8 = if (uses_other) other else model;
        const position_at: usize = if (uses_other) 0x174 else 0x1c;
        const depth_at: usize = if (uses_other) 0x178 else 0x20;
        var other_size = ctx.range(u16, 0, 0x30);
        if (ctx.chance(3)) other_size = 0;
        put16(other, 0x94, other_size);
        other[0xb3] = ctx.range(u8, 0, 0x20);
        const other_height: u16 = ctx.range(u16, 0, 0x60);
        put16(other, 0xb8, other_height);
        const reach: i32 = @as(i32, other_size) + other[0xb3] + self_size;
        const half: i32 = (@as(i32, self_height) + other_height) >> 1;
        // Inside offsets, signs consistent with the direction.
        var ox: i32 = ctx.edge(i32, &.{ 0, reach, 1, ctx.range(i32, 0, reach) });
        var oz: i32 = ctx.edge(i32, &.{ 0, reach, 1, ctx.range(i32, 0, reach) });
        var oy: i32 = ctx.edge(i32, &.{ 0, half, -half, 1, ctx.range(i32, -half, half) });
        if (dx < 0 or (dx == 0 and ctx.chance(2))) ox = -ox;
        if (dz < 0 or (dz == 0 and ctx.chance(2))) oz = -oz;
        if (is_target or (id == state[0x88] and !behind)) {
            // inside
        } else if (behind) {
            if (dx != 0) ox = -ox else oz = -oz;
            if (dx != 0 and ox == 0) ox = if (dx > 0) -1 else 1;
            if (dx == 0 and oz == 0) oz = if (dz > 0) -1 else 1;
        } else {
            switch (ctx.range(u8, 0, 2)) {
                0 => ox = if (ctx.chance(2)) reach + 1 else -(reach + 1),
                1 => oy = if (ctx.chance(2)) half + 1 else -(half + 1),
                else => oz = if (ctx.chance(2)) reach + 1 else -(reach + 1),
            }
            // A miss beyond the reach on the wrong side is skipped by the
            // direction test anyway; both are misses.
        }
        const px: i32 = std.math.clamp(x + ox, 0, 0xFFFF);
        const pz: i32 = std.math.clamp(z + oz, 0, 0xFFFF);
        const py: i16 = @intCast(std.math.clamp(height + oy, -0x8000, 0x7FFF));
        put32(holder, position_at, @as(u32, @intCast(px)) | (@as(u32, @as(u16, @bitCast(py))) << 16));
        put16(holder, depth_at, @intCast(pz));
    }
    v.g.vs_battle_actors()[0] = if (first) |f| @ptrCast(f) else null;
    return .{ state, @as(c_int, dir) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return stage;
}
