//! zig_diff spec: func_800DB93C -> pc.resolve_flagged_cell.resolveFlaggedCell (D2: pc/src/battle_movement.c).
//! Inputs: a 256 x 32 grid of cells (scratchpad 0x3C0) random except along the
//! walk: the start tile (byte 3 of the argument random, byte 1 too) unflagged
//! (0x20 clear: returned as is), or flagged with the step byte 0xB2 zero (the
//! walk stops at the first cell), or flagged with the limit 1..255 and 0..4
//! further flagged cells before the clear one, so the walk ends before, at or
//! past min(limit, 3) steps; the packed direction (0x3EC + dir) with random
//! bits and at least one nonzero step (the walk wraps at 0xFF). The stubs of
//! func_800D96C8 (the step result) and func_800D954C (the entry result) answer
//! from the spec, 1 included, and record their arguments.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").resolveFlaggedCell));

pub const classes = [_][:0]const u8{ "unflagged", "limit-zero", "step-only", "step-and-entry", "past-step-limit" };

var stage: usize = 0;
var step_result: c_int = 0;
var enter_result: c_int = 0;

pub const stubs = struct {
    pub fn func_800D96C8(ctx: *zd.Ctx, state: anytype, from: anytype, to: anytype, dir: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = from;
        _ = to;
        _ = dir;
        return step_result;
    }
    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, tile: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = tile;
        return enter_result;
    }
};

fn signed2(bits: u8) i32 {
    return switch (bits & 3) {
        0 => 0,
        1 => 1,
        2 => -2,
        else => -1,
    };
}

fn cellAt(cells: [*]u8, x: i32, z: i32) *align(1) u16 {
    return @ptrCast(cells + @as(usize, @intCast(z)) * 64 + @as(usize, @intCast(x)) * 2);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]u8 = @ptrFromInt(zd.scratchpad);
    stage = ctx.range(usize, 0, 4);
    const dir = ctx.range(c_int, 0, 7);
    const cells = ctx.allocBytes(256 * 64, 16);
    for (cells[0 .. 256 * 64]) |*byte| byte.* = ctx.int(u8);
    std.mem.writeInt(u32, scratch[0x3c0..][0..4], @intCast(@intFromPtr(cells)), .little);
    var packed_direction: u8 = ctx.int(u8);
    if (packed_direction & 0x33 == 0) packed_direction |= ctx.edge(u8, &.{ 1, 2, 3, 0x10, 0x20, 0x30, 0x11 });
    scratch[0x3ec + @as(usize, @intCast(dir))] = packed_direction;
    const dx = signed2(packed_direction);
    const dz = signed2(packed_direction >> 4);

    const state = ctx.allocBytes(0x100, 16);
    for (state[0..0x100]) |*byte| byte.* = ctx.int(u8);
    const x0: i32 = ctx.range(i32, 0, 255);
    const z0: i32 = ctx.range(i32, 0, 31);
    // Flagged cells before the clear one.
    var flagged: i32 = ctx.range(i32, 0, 4);
    var limit: u8 = ctx.edge(u8, &.{ 1, 2, 3, 4, 255 });
    switch (stage) {
        0 => {},
        1 => limit = 0,
        2, 3 => {
            limit = ctx.edge(u8, &.{ 3, 3, 4, 255, 2 });
            flagged = if (limit == 2) 0 else ctx.range(i32, 0, 1);
        },
        else => {
            limit = ctx.edge(u8, &.{ 1, 2, 3, 4, 255 });
            flagged = switch (limit) {
                1 => ctx.range(i32, 0, 2),
                2 => ctx.range(i32, 1, 3),
                else => ctx.range(i32, 2, 4),
            };
        },
    }
    state[0xb2] = limit;
    cellAt(cells, x0, z0).* &= ~@as(u16, 0x20);
    if (stage != 0) {
        cellAt(cells, x0, z0).* |= 0x20;
        var x = x0;
        var z = z0;
        var i: i32 = 0;
        // The oracle's z wraps at 256 rows: the grid has 256 rows of 64 bytes.
        while (i <= flagged) : (i += 1) {
            x = (x + dx) & 0xff;
            z = (z + dz) & 0xff;
            const cell = cellAt(cells, x, z);
            if (i < flagged) cell.* |= 0x20 else cell.* &= ~@as(u16, 0x20);
        }
    }
    step_result = if (stage == 3) 1 else ctx.edge(c_int, &.{ 0, 2, -1, 0x100, 0 });
    enter_result = ctx.edge(c_int, &.{ 0, 1, -1, 2, 0xFF });
    const tile: c_uint = @as(c_uint, @intCast(x0)) | (@as(c_uint, ctx.int(u8)) << 8) | (@as(c_uint, @intCast(z0)) << 16) | (@as(c_uint, ctx.int(u8)) << 24);
    return .{ state, tile, dir, ctx.int(c_uint) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return stage;
}
