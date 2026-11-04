//! zig_diff spec: func_800DC3CC -> pc.candidate_sort.sortActorCandidates (D2: pc/src/battle_helpers.c, through its static sort_candidates).
//! Inputs: a state block with the count at 0x470 (0, 1, 2, 3, 5, 8, 16, any
//! of 0..16, or negative: the sort then does nothing) and the candidate
//! pointer list at 0x430 holding guest pointers to arena candidates whose
//! first word (the score) comes from a small pool, so ties and strictly
//! ordered neighbours are common, signed extremes included; a pointer may
//! appear twice. More than 256 entries would make the count word itself entry
//! 16 of the list (a bad pointer), so that guard cannot be driven.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").sortActorCandidates));

pub const classes = [_][:0]const u8{ "nothing-to-sort", "one-entry", "several-entries" };

const scores = [_]i32{ 0, 1, 2, -1, 5, 0x7FFFFFFF, -0x80000000 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x500, 16);
    for (state[0..0x500]) |*byte| byte.* = ctx.int(u8);
    const count = ctx.edge(i32, &.{ 0, 1, 1, 2, 3, 5, 8, 16, 16, ctx.range(i32, 0, 16), ctx.range(i32, 0, 16), -1, -5 });
    std.mem.writeInt(i32, state[0x470..][0..4], count, .little);
    var pool: [16]u32 = undefined;
    for (&pool) |*slot| {
        const candidate = ctx.allocBytes(0x20, 16);
        for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(i32, candidate[0..4], if (ctx.chance(2)) ctx.edge(i32, &.{ 0, 1, 2 }) else ctx.pick(i32, &scores), .little);
        slot.* = @intCast(@intFromPtr(candidate));
    }
    for (0..16) |i| {
        const pointer = if (ctx.chance(8)) pool[ctx.range(usize, 0, 15)] else pool[i];
        std.mem.writeInt(u32, state[0x430 + i * 4 ..][0..4], pointer, .little);
    }
    // The word before the list is a valid candidate pointer too, so a sort
    // that starts one word early (a mutation) reorders memory and mismatches
    // instead of dereferencing random bytes and crashing.
    std.mem.writeInt(u32, state[0x42c..][0..4], pool[ctx.range(usize, 0, 15)], .little);
    return .{state};
}

pub fn classify(args: Args) usize {
    const count = std.mem.readInt(i32, args[0][0x470..][0..4], .little);
    return if (count <= 0) 0 else if (count == 1) 1 else 2;
}
