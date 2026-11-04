//! zig_diff spec: func_800D954C -> pc.tile_enterable.tileEnterable (D2: pc/src/battle_movement.c).
//! Inputs, built for one outcome at a time: the tile (bytes 0 and 2 the
//! coordinates, byte 1 random, byte 3 random) equal to the reserved tile
//! (0x38, on its low 24 bits with byte 1 cleared), outside the grid
//! (scratchpad 0x3DC/0x3DD), on a cell with a blocked flag (0x10, 0x20, 0x80,
//! 0x8000 or a bit of the actor's mask byte 0x32 in bits 8-15), overlapped by
//! another actor (distances at, inside and beyond the reach, the height and
//! the reach from a model or from the actor's AI record, which is sometimes
//! missing: the oracle then reads PS1 address 0), or free of all of them. The
//! stub of func_800E42EC answers the floor height from the spec.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tileEnterable));

pub const classes = [_][:0]const u8{ "reserved", "outside-grid", "cell-blocked", "actor-overlaps", "free" };

var stage: usize = 0;
var height: c_int = 0;

pub const stubs = struct {
    pub fn func_800E42EC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = ctx;
        _ = x;
        _ = z;
        return height;
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

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]u8 = @ptrFromInt(zd.scratchpad);
    const low_ram: [*]u8 = @ptrFromInt(zd.low_ram);
    stage = ctx.range(usize, 0, 4);
    height = ctx.pick(c_int, &.{ 0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000 });
    const width = ctx.range(u8, 1, 12);
    const depth_cells = ctx.range(u8, 1, 12);
    scratch[0x3dc] = width;
    scratch[0x3dd] = depth_cells;
    const cells = randomBlock(ctx, 16 * 64);
    put32(scratch, 0x3c0, @intCast(@intFromPtr(cells)));
    const state = randomBlock(ctx, 0x200);
    state[0x88] = ctx.range(u8, 0, 16);
    var self_size: u16 = ctx.range(u16, 0, 0x30);
    if (ctx.chance(3)) self_size = 0;
    put16(state, 0x94, self_size);

    // The tile.
    var x: u8 = ctx.range(u8, 0, width - 1);
    var z: u8 = ctx.range(u8, 0, depth_cells - 1);
    if (stage == 1) {
        switch (ctx.range(u8, 0, 3)) {
            0 => x = ctx.range(u8, width, 255),
            1 => z = ctx.range(u8, depth_cells, 255),
            2 => {
                x = width;
                z = depth_cells -% 1;
            },
            else => {
                x = width -% 1;
                z = depth_cells;
            },
        }
    }
    const tile: u32 = @as(u32, x) | (@as(u32, ctx.edge(u8, &.{ 0, 0, 1, 0x80, ctx.int(u8) })) << 8) | (@as(u32, z) << 16) | (@as(u32, ctx.int(u8)) << 24);
    const plain: u32 = @as(u32, x) | (@as(u32, z) << 16);
    if (stage == 0) {
        put32(state, 0x38, plain | (@as(u32, ctx.int(u8)) << 24));
    } else {
        var reserved = ctx.int(u32);
        if (ctx.chance(3)) reserved = (tile ^ (@as(u32, 1) << ctx.range(u5, 0, 23))) & 0xFFFFFF; // one bit off
        if (ctx.chance(4)) reserved = tile & 0xFFFFFF; // equal with byte 1 kept
        if (reserved & 0xFFFFFF == plain) reserved ^= 0x10000;
        put32(state, 0x38, reserved);
    }

    // The cell.
    // Outside the grid the cell is made enterable when it still lies inside
    // the block, so a grid test that is off by one is seen as a different
    // answer (free) rather than hidden behind a blocked random cell.
    if ((x < width and z < depth_cells) or (stage == 1 and x < 32 and z < 16)) {
        const blocked_bits: u16 = (@as(u16, state[0x32]) << 8) | 0x80b0;
        var cell = ctx.int(u16);
        if (stage == 2) {
            const bits = [_]u16{ 0x10, 0x20, 0x80, 0x8000, @as(u16, 1) << ctx.range(u4, 8, 15) & blocked_bits };
            cell |= bits[ctx.range(usize, 0, bits.len - 1)];
            if (cell & blocked_bits == 0) cell |= 0x10;
        } else {
            cell &= ~blocked_bits;
        }
        put16(cells, @as(usize, z) * 64 + @as(usize, x) * 2, cell);
    }

    // The actors.
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = randomBlock(ctx, 0x80);
        table[i] = @ptrCast(model);
        put32(scratch, 0x37c + 4 * i, if (ctx.chance(4)) 0 else @intCast(@intFromPtr(randomBlock(ctx, 0x200))));
    }
    for (0..0x300) |i| low_ram[0x80 + i] = ctx.int(u8);
    const point_x: i32 = @as(i32, x) * 128 + 64;
    const point_z: i32 = @as(i32, z) * 128 + 64;
    const count = if (stage == 3) ctx.range(usize, 1, 4) else ctx.range(usize, 0, 4);
    const overlapping = if (stage == 3) ctx.range(usize, 0, count - 1) else count;
    var first: ?[*]u8 = null;
    var previous: ?[*]u8 = null;
    for (0..count) |i| {
        var id = ctx.range(usize, 0, 16);
        if (i == overlapping and id == state[0x88]) id = (id + 1) % 17;
        if (i != overlapping and ctx.chance(4)) id = state[0x88];
        const node = randomBlock(ctx, 0x10);
        put32(node, 0, 0);
        put32(node, 4, @intCast(id));
        if (previous) |p| put32(p, 0, @intCast(@intFromPtr(node))) else first = node;
        previous = node;
        // The actor itself is skipped by the oracle: it is placed on the tile.
        const is_self = id == state[0x88];
        const model = table[id].?;
        const model_bytes: [*]u8 = @ptrCast(model);
        const slot = std.mem.readInt(u32, scratch[0x37c + 4 * id ..][0..4], .little);
        const other: [*]u8 = if (slot == 0) low_ram else @ptrFromInt(slot);
        const uses_other = id != 0 and model_bytes[0xa] & 0x1f != 0;
        const holder: [*]u8 = if (uses_other) other else model_bytes;
        const position_at: usize = if (uses_other) 0x174 else 0x1c;
        const depth_at: usize = if (uses_other) 0x178 else 0x20;
        var other_size = ctx.range(u16, 0, 0x30);
        if (ctx.chance(3)) other_size = 0;
        put16(other, 0x94, other_size);
        const reach: i32 = @as(i32, other_size) + self_size;
        // Distances from the tile centre: inside (at the reach included) for
        // the overlapping actor, one axis beyond it for the others.
        var ddx: i32 = ctx.edge(i32, &.{ 0, reach, -reach, 1, -1, ctx.range(i32, -reach, reach) });
        var ddy: i32 = ctx.edge(i32, &.{ 0, 0x80, -0x80, 5, -5 });
        var ddz: i32 = ctx.edge(i32, &.{ 0, reach, -reach, 1, -1, ctx.range(i32, -reach, reach) });
        if (i != overlapping and !is_self) {
            switch (ctx.range(u8, 0, 2)) {
                0 => ddx = if (ctx.chance(2)) reach + 1 else -(reach + 1),
                1 => ddy = if (ctx.chance(2)) 0x81 else -0x81,
                else => ddz = if (ctx.chance(2)) reach + 1 else -(reach + 1),
            }
        }
        ddx = @max(ddx, -point_x);
        if (i != overlapping and !is_self and ddx == -point_x and reach + 1 > point_x) ddx = reach + 1;
        const px: u16 = @intCast(point_x + ddx);
        const py: i16 = @intCast(std.math.clamp(@as(i32, height) + ddy, -0x8000, 0x7FFF));
        put32(holder, position_at, @as(u32, px) | (@as(u32, @as(u16, @bitCast(py))) << 16));
        put16(holder, depth_at, @intCast(@max(point_z + ddz, 0)));
    }
    v.g.vs_battle_actors()[0] = if (first) |f| @ptrCast(f) else null;
    return .{ state, tile };
}

pub fn classify(args: Args) usize {
    _ = args;
    return stage;
}
