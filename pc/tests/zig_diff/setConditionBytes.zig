//! zig_diff spec: func_800E4F14 -> pc.actor_conditions.setConditionBytes (D2: pc/src/battle_helpers.c).
//! Inputs: a state block with the threshold byte at 0x17 zero or small, and
//! its stats block (through the words at 0x54 and 0x3C) whose pairs at 0x18
//! and 0x1C have the low halfword twice around the high one (below, equal,
//! above) and around the threshold, and whose flags at 0x948 have each of
//! the status bits 0xE2B0 and 0x1000 set or clear. The output bytes at
//! 0x10-0x12 start random.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setConditionBytes));

pub const classes = [_][:0]const u8{ "threshold-zero", "threshold-set" };

fn put(block: [*]u8, comptime T: type, offset: usize, value: T) void {
    std.mem.writeInt(T, block[offset..][0..@sizeOf(T)], value, .little);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x100, 16);
    for (state[0..0x100]) |*byte| byte.* = ctx.int(u8);
    const record = ctx.allocBytes(0x40, 16);
    for (record[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const stats = ctx.allocBytes(0x960, 16);
    for (stats[0..0x960]) |*byte| byte.* = ctx.int(u8);
    put(state, u32, 0x54, @intCast(@intFromPtr(record)));
    put(record, u32, 0x3c, @intCast(@intFromPtr(stats)));

    const threshold: u8 = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 2, 5, 40, 100 });
    state[0x17] = threshold;

    // First pair: low * 2 against high, equal a quarter of the time.
    const high1: u16 = ctx.range(u16, 0, 0x4000);
    const low1: u16 = switch (ctx.range(u8, 0, 3)) {
        0 => high1 / 2,
        1 => high1 / 2 + 1,
        2 => if (high1 / 2 > 0) high1 / 2 - 1 else 0,
        else => ctx.int(u16) & 0x7fff,
    };
    put(stats, u32, 0x18, (@as(u32, high1) << 16) | low1);

    // Second pair: low against the threshold, and low * 2 against high.
    const high2: u16 = ctx.range(u16, 0, 0x4000);
    const low2: u16 = switch (ctx.range(u8, 0, 4)) {
        0 => threshold,
        1 => if (threshold > 0) threshold - 1 else 0,
        2 => threshold +| 1,
        3 => high2 / 2,
        else => ctx.int(u16) & 0x7fff,
    };
    put(stats, u32, 0x1c, (@as(u32, high2) << 16) | low2);

    var flags: u32 = ctx.int(u32) & ~@as(u32, 0xe2b0 | 0x1000);
    if (ctx.chance(2)) flags |= ctx.edge(u32, &.{ 0x80, 0x20, 0x10, 0x2000, 0x4000, 0x8000, 0x200, 0x2b0, 0xe2b0 }) & 0xe2b0;
    if (ctx.chance(2)) flags |= 0x1000;
    put(stats, u32, 0x948, flags);
    return .{state};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0][0x17] != 0);
}
