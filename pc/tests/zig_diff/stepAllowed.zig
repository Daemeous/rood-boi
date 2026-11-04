//! zig_diff spec: func_800D96C8 -> pc.step_allowed.stepAllowed (D2: pc/src/battle_movement.c).
//! Inputs, built for one outcome at a time (the stage): the actor blocked
//! (byte 7), the target tile outside the grid (scratchpad 0x3DC/0x3DD), on a
//! cell with flags 0x30, or of another area code than byte 0x132 (byte 2
//! clear); a neighbouring step blocked by an actor (the stub of func_800D98E8);
//! a step too far for byte 0xB2 (the X step negative too: the oracle negates
//! the Z step then); a rise above the limit while climbing (byte 3 odd; an
//! even value passes the bitwise test), or below minus the limit (the limit
//! 0x2000 when byte 0x15E has bits 3-7, else the halfword at 0xB0); no
//! approach (byte 5 clear); the approach radius test (mode byte 0x89 == 1,
//! radius from word 0xDC, squared in 32 bits) landing below, on and above the
//! square; and the final distance comparison, which the stubs of
//! func_800E4660/func_800E45F4 answer. The stub of func_800E42EC gives the
//! floor height of the target and of the origin tile.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").stepAllowed));

pub const classes = [_][:0]const u8{
    "blocked-actor",     "outside-grid", "cell-blocked", "area-mismatch", "neighbour-blocked", "too-far",
    "climb-limit",       "drop-limit",   "no-approach",  "approach-radius", "closer",
};

var stage: usize = 0;
var to_x: c_int = 0;
var to_z: c_int = 0;
var height_to: c_int = 0;
var height_from: c_int = 0;
var blocker: c_int = 0;
var approach_value: c_int = 0;
var next_value: c_int = 0;
var current_value: c_int = 0;

pub const stubs = struct {
    pub fn func_800E42EC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = ctx;
        return if (x == to_x and z == to_z) height_to else height_from;
    }
    pub fn func_800D98E8(ctx: *zd.Ctx, state: anytype, dir: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = dir;
        return blocker;
    }
    pub fn func_800E45F4(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        return current_value;
    }
    pub fn func_800E4660(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype) c_int {
        _ = ctx;
        _ = x;
        _ = z;
        // The approach test passes a nonzero height difference (the Z
        // distance), the final comparison a literal 0.
        return if (y != 0) approach_value else next_value;
    }
};

fn put16(p: [*]u8, off: usize, value: u16) void {
    std.mem.writeInt(u16, p[off..][0..2], value, .little);
}

fn put32(p: [*]u8, off: usize, value: u32) void {
    std.mem.writeInt(u32, p[off..][0..4], value, .little);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]u8 = @ptrFromInt(zd.scratchpad);
    stage = ctx.range(usize, 0, 10);
    const width = ctx.range(u8, 2, 12);
    const depth_cells = ctx.range(u8, 2, 12);
    scratch[0x3dc] = width;
    scratch[0x3dd] = depth_cells;
    const cells = ctx.allocBytes(16 * 64, 16);
    for (cells[0 .. 16 * 64]) |*byte| byte.* = ctx.int(u8);
    put32(scratch, 0x3c0, @intCast(@intFromPtr(cells)));
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = ctx.allocBytes(0x40, 16);
        for (model[0..0x40]) |*byte| byte.* = ctx.int(u8);
        table[i] = @ptrCast(model);
    }

    state[7] = if (stage == 0) ctx.range(u8, 1, 255) else 0;
    var x: u8 = ctx.range(u8, 0, width - 1);
    var z: u8 = ctx.range(u8, 0, depth_cells - 1);
    if (stage == 1) {
        switch (ctx.range(u8, 0, 3)) {
            0 => x = ctx.edge(u8, &.{ width, ctx.range(u8, width, 255) }),
            1 => z = ctx.edge(u8, &.{ depth_cells, ctx.range(u8, depth_cells, 255) }),
            2 => z = depth_cells,
            else => {
                x = width;
                z = depth_cells;
            },
        }
    }
    var cell: u16 = ctx.int(u16);
    cell = if (stage == 2) cell | ctx.edge(u16, &.{ 0x10, 0x20, 0x30 }) else cell & ~@as(u16, 0x30);
    const in_grid = x < width and z < depth_cells;
    if (in_grid) put16(cells, @as(usize, z) * 64 + @as(usize, x) * 2, cell);
    state[2] = if (stage == 3) 0 else ctx.edge(u8, &.{ 0, 1, 2, 0xFF });
    state[0x132] = if (stage == 3) @intCast(((cell & 0xf) + ctx.range(u16, 1, 15)) & 0xf) else @intCast(cell & 0xf);
    if (ctx.chance(3) and stage != 3) state[0x132] = ctx.int(u8); // matters only when byte 2 is set

    // The step: from = to - step.
    var step_x: i32 = ctx.edge(i32, &.{ 0, 0, 1, -1, 1, -1, 2, -2, 3, -3, 5 });
    var step_z: i32 = ctx.edge(i32, &.{ 0, 0, 1, -1, 1, -1, 2, -2, 3, -3, 5 });
    const small = (stage == 4) or (stage >= 8 and ctx.chance(2)) or (stage == 7 and ctx.chance(3));
    if (small) {
        step_x = ctx.edge(i32, &.{ 0, 1, -1, -4 });
        step_z = ctx.edge(i32, &.{ 0, 1, -1, -1 });
    }
    if (stage == 5 and ctx.chance(2)) {
        step_x = ctx.edge(i32, &.{ 0, 1 });
        step_z = 1;
    }
    if (@as(i32, x) - step_x < 0 or @as(i32, x) - step_x > 255) step_x = 0;
    if (@as(i32, z) - step_z < 0 or @as(i32, z) - step_z > 255) step_z = 0;
    const from: u32 = @as(u32, @intCast(@as(i32, x) - step_x)) | (@as(u32, @intCast(@as(i32, z) - step_z)) << 16) | (@as(u32, ctx.int(u8)) << 8) | (@as(u32, ctx.int(u8)) << 24);
    const to: u32 = @as(u32, x) | (@as(u32, ctx.int(u8)) << 8) | (@as(u32, z) << 16) | (@as(u32, ctx.int(u8)) << 24);
    to_x = x;
    to_z = z;
    // The oracle's own step quantities.
    const sx: i32 = step_x;
    var sz: i32 = step_z;
    if (sx < 0) sz = -sz;
    if (sz < 0) sz = -sz;
    const nearby = sx < 2 and sz < 2;
    blocker = if (stage == 4) ctx.edge(c_int, &.{ 1, 2, -1 }) else 0;

    // The limits and the rise.
    state[3] = ctx.edge(u8, &.{ 0, 0, 1, 1, 2, 3, 0xFF });
    state[0xb2] = ctx.range(u8, 0, 12);
    if (stage == 5) state[0xb2] = @intCast(@max(0, sx + sz - (if (nearby) 1 else ctx.range(i32, 1, 3))));
    state[0x15e] = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 0x08, 0xF8, 0x80, 0x07 });
    const limit: i32 = if (state[0x15e] & 0xf8 != 0) 0x2000 else ctx.edge(i32, &.{ 0, 1, 0x100, 0x2000, 0x7FFF, 0xFFFF });
    put16(state, 0xb0, @intCast(if (state[0x15e] & 0xf8 != 0) ctx.int(u16) else limit));
    var rise: i32 = ctx.edge(i32, &.{ 0, 1, -1, 5, -5, limit, -limit, limit - 1, -limit + 1, 0x300, -0x300 });
    switch (stage) {
        6 => {
            state[3] = ctx.edge(u8, &.{ 1, 3, 0xFF, 1 });
            rise = limit + ctx.edge(i32, &.{ 1, 2, 100 });
        },
        7 => rise = -limit - ctx.edge(i32, &.{ 1, 2, 100 }),
        else => {},
    }
    if (stage == 5 and nearby) {
        // A flat or falling near step is not "checked": the step limit applies.
        state[3] = 0;
        rise = ctx.edge(i32, &.{ 0, 0, -1, -5 });
    }
    if (stage >= 8) {
        // Passing every limit test when they are evaluated.
        if (!(nearby and state[3] == 0 and rise >= 1)) {
            state[0xb2] = @intCast(@min(255, @max(0, sx + sz) + ctx.range(i32, 0, 3)));
            // Only an odd climbing byte makes the rise limit apply (a bitwise AND).
            if (state[3] & 1 != 0) rise = @min(rise, limit);
            rise = @max(rise, -limit);
        }
    }
    height_from = ctx.pick(c_int, &.{ 0, 0x1000, -0x1000, 77 });
    height_to = height_from +% rise;

    state[5] = if (stage == 8) 0 else ctx.edge(u8, &.{ 1, 2, 0xFF });
    state[0x89] = if (stage == 9) 1 else ctx.edge(u8, &.{ 1, 1, 0, 2, 5 });
    state[0xe5] = ctx.range(u8, 0, 16);
    const radius_base = ctx.edge(c_int, &.{ 0x60, 0x61, 0x5F, 0x100, 0x7FFFFFFF, 0, -0x80000000 });
    put32(state, 0xdc, @bitCast(radius_base));
    const radius: c_int = radius_base -% 0x60;
    const square: c_int = @bitCast(@as(c_uint, @bitCast(radius)) *% @as(c_uint, @bitCast(radius)));
    approach_value = switch (stage) {
        9 => square -% ctx.edge(c_int, &.{ 1, 5 }),
        else => square +% ctx.edge(c_int, &.{ 0, 1, 5 }),
    };
    if (ctx.chance(8)) approach_value = ctx.int(c_int);
    next_value = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 });
    current_value = next_value +% ctx.edge(c_int, &.{ 0, 0, 1, -1, 7 });
    return .{ state, from, to, ctx.range(c_int, 0, 7) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (stage > 10) 10 else stage;
}
