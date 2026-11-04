//! zig_diff spec: func_800E4DF8 -> pc.actor_conditions.slotUsable (D2: pc/src/battle_helpers.c).
//! Inputs: a state block whose word at 0x54 points at a record whose word at
//! 0x3C points at a random-filled stats block (action of each slot at 0x8C0,
//! row counts at 0x398 + row * 0xDC, the pair at 0x18, the resources at 0x1C
//! and 0x14C, the flags at 0x948), the action table vs_main_actions with the
//! chosen action's type (bits 1-3 of byte 2) and cost (byte 3), and a row and
//! column 0..3. The counts are around 2, the resources and the pair around
//! the cost (below, equal, above), the flags have 0x2000 and 0x1000 set or
//! clear, and the action index is below 256.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").slotUsable));

pub const classes = [_][:0]const u8{ "count-low", "blocked-state", "type-1", "type-3", "type-4", "other-type" };

var which: usize = 0;

fn put(block: [*]u8, comptime T: type, offset: usize, value: T) void {
    std.mem.writeInt(T, block[offset..][0..@sizeOf(T)], value, .little);
}

fn near(ctx: *zd.Ctx, target: i32) i16 {
    return @intCast(target + ctx.range(i32, -1, 1));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    const record = ctx.allocBytes(0x40, 16);
    for (record[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const stats = ctx.allocBytes(0xa00, 16);
    for (stats[0..0xa00]) |*byte| byte.* = ctx.int(u8);
    put(state, u32, 0x54, @intCast(@intFromPtr(record)));
    put(record, u32, 0x3c, @intCast(@intFromPtr(stats)));

    const row = ctx.range(c_int, 0, 3);
    const column = ctx.range(c_int, 0, 3);
    const slot: usize = @intCast(row * 4 + column);
    const action = ctx.range(u16, 0, 255);
    put(stats, u16, 0x8c0 + slot * 4, action);
    put(stats, i16, 0x398 + @as(usize, @intCast(row)) * 0xdc, ctx.edge(i16, &.{ -1, 0, 1, 2, 2, 3, 100 }));

    const entry = v.g.actions() + @as(usize, action) * 0x34;
    const kind = ctx.range(u8, 0, 7);
    entry[2] = (entry[2] & 0xf1) | (kind << 1);
    const cost = ctx.range(u8, 0, 100);
    entry[3] = cost;

    var flags = ctx.int(u32) & ~@as(u32, 0x3000);
    if (ctx.chance(2)) flags |= 0x2000;
    if (ctx.chance(2)) flags |= 0x1000;
    put(stats, u32, 0x948, flags);
    put(stats, i16, 0x1c, near(ctx, cost));
    put(stats, i16, 0x14c, near(ctx, cost));
    const high: u16 = ctx.range(u16, 0, 200);
    const low: i32 = @as(i32, cost) + (high >> 1) + ctx.range(i32, -1, 1);
    put(stats, u32, 0x18, (@as(u32, high) << 16) | @as(u32, @intCast(@max(low, 0))));

    const row_count = std.mem.readInt(i16, stats[0x398 + @as(usize, @intCast(row)) * 0xdc ..][0..2], .little);
    which = if (row_count < 2)
        0
    else if (flags & 0x2000 != 0 and (@as(u32, 1) << @intCast(kind)) & 6 == 0)
        1
    else switch (kind) {
        1 => 2,
        3 => 3,
        4 => 4,
        else => 5,
    };
    return .{ state, row, column };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
