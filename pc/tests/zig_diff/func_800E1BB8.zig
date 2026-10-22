//! zig_diff spec: func_800E1BB8 -> func_800E1BB8.func_800E1BB8 (771A8.c).
//! Inputs: a wanderState in the arena with a fresh or running count, flags with
//! bit 8 set or clear, flag2 zero or not, an angle around zero and negative,
//! goal bytes at the edges, and a maxCount that is 0, count+1 (so the
//! comparison sees equal sides) or an edge value. The scratchpad holds the
//! byte-table pointer, its stride, the 0x1F8003F4 switch and the WANDER_TILES
//! pointer, with the tiles all blocked, all open, or random. The three callees
//! are recording stubs that return zero and nonzero values and write the state
//! the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1BB8));

pub const classes = [_][:0]const u8{ "zero-max", "fresh-state", "flags-8", "running" };

pub const stubs = struct {
    pub fn func_800E4690(ctx: *zd.Ctx, raw: anytype, dir: anytype) c_int {
        _ = raw;
        _ = dir;
        const r = ctx.value(u32);
        return if (r % 3 == 0) 0 else @as(c_int, @bitCast(r));
    }

    pub fn func_800D96C8(ctx: *zd.Ctx, state: anytype, raw: anytype, cand: anytype, dir: anytype) c_int {
        _ = raw;
        _ = cand;
        _ = dir;
        const s: *v.wanderState = @ptrCast(@alignCast(state.?));
        // The callee can change what the C reads after the call: flag2, the
        // flags (read at the next direction) and the goal.
        if (ctx.value(u2) == 0) s.flag2 = ctx.value(u8);
        if (ctx.value(u2) == 0) s.flags = ctx.value(u16);
        if (ctx.value(u2) == 0) s.goal._bytes[0] = ctx.value(u8);
        const r = ctx.value(u8);
        return if (r < 128) 0 else @as(c_int, r);
    }

    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.value(c_int);
    }
};

fn scratch(comptime T: type, off: usize) *align(1) T {
    return @ptrFromInt(zd.scratchpad + off);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.wanderState);
    const tiles = ctx.allocBytes(32768, 16);
    const bytes = ctx.allocBytes(65536, 16);

    // WANDER_TILES pointer, byte-table pointer, byte-table stride, and the
    // switch the C tests for the byte table.
    scratch(u32, 0x3C0).* = psx.addr(tiles);
    scratch(u32, 0x3C4).* = psx.addr(bytes);
    scratch(u8, 0x3DC).* = ctx.pick(u8, &.{ 0, 1, 2, 31, 32, 64, 128, 255 });
    scratch(u32, 0x3F4).* = if (ctx.chance(2)) 0 else ctx.pick(u32, &.{ 1, 0x8000_0000, 0xFFFF_FFFF, 0x100 });

    if (ctx.chance(6)) {
        @memset(tiles[0..32768], 0xFF);
    } else if (ctx.chance(6)) {
        @memset(tiles[0..32768], 0);
    }

    state.flags = ctx.pick(u16, &.{ 0, 8, 0xFFFF, 0x8000 });
    state.flag2 = ctx.pick(u8, &.{ 0, 1, 0xFF });
    state.count = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 2, 127, 128, 254, 255 });
    state.angle = ctx.pick(i16, &.{ 0, 1, 2, 3, 4, 6, 7, -1, -4, -2, 32767, -32768 });
    state.goal._bytes = .{
        ctx.pick(u8, &.{ 0, 1, 2, 31, 32, 127, 128, 254, 255 }),
        ctx.pick(u8, &.{ 0, 1, 2, 31, 32, 127, 128, 254, 255 }),
        ctx.pick(u8, &.{ 0, 1, 2, 31, 32, 127, 128, 254, 255 }),
        ctx.int(u8),
    };

    const tile = ctx.pick(c_uint, &.{ 0, 0x0102_0304, 0xFFFF_FFFF, 0x00FF_00FF });
    var maxCount = ctx.pick(c_uint, &.{ 1, 2, 3, 4, 255, 256, 0xFFFF_FFFF });
    if (ctx.chance(3)) maxCount = @as(c_uint, state.count) + 1;
    if (ctx.chance(8)) maxCount = 0;

    return .{ state, tile, maxCount, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (args[2] == 0) return 0;
    const state = args[0].?;
    if (state.count == 0) return 1;
    if ((state.flags & 8) != 0) return 2;
    return 3;
}
