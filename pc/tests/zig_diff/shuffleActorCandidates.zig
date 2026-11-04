//! zig_diff spec: func_800DC574 -> pc.candidate_sort.shuffleActorCandidates (D2: pc/src/battle_helpers.c, through its static sort_candidates).
//! Inputs: a state block whose list at 0x430 holds 20 guest pointers to
//! arena candidates (entries 16-19 overlap the words at 0x470-0x47C, so a
//! count over 16, which pc_unimplemented reports, stays inside valid
//! pointers), a count at the edges around 16 (or negative: no draw), the
//! action byte (word 1 bits 5-12) zero a third of the time, weights (word 3
//! bits 19-26) around the 0x4C cap, scores (word 0) from a small pool, and
//! the stub of rand returning, for the draw of candidate i, a roll just
//! under, equal to, or just over its capped weight, or any value (the high
//! bits are random: only the low 7 are used). The first candidate that wins
//! swaps with entry 0 and the rest is re-sorted.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").shuffleActorCandidates));

pub const classes = [_][:0]const u8{ "no-draw", "no-winner", "winner-first", "winner-later", "over-16" };

var which: usize = 0;
var rolls: [24]u32 = @splat(0);
var skipped_calls: u32 = 0;

const scores = [_]i32{ 0, 1, 2, -1, 5, 0x7FFFFFFF, -0x80000000 };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x500, 16);
    for (state[0..0x500]) |*byte| byte.* = ctx.int(u8);
    const count = ctx.edge(c_int, &.{ 0, 1, 2, 3, 4, 5, 8, 15, 16, 16, 17, 18, 20, -1 });
    var weights: [20]u32 = undefined;
    var actions: [20]u32 = undefined;
    for (0..20) |i| {
        const candidate = ctx.allocBytes(0x20, 16);
        for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(i32, candidate[0..4], if (ctx.chance(2)) ctx.edge(i32, &.{ 0, 1, 2 }) else ctx.pick(i32, &scores), .little);
        var word1 = std.mem.readInt(u32, candidate[4..8], .little);
        if (ctx.chance(3)) word1 &= ~(@as(u32, 0xff) << 5);
        std.mem.writeInt(u32, candidate[4..8], word1, .little);
        actions[i] = (word1 >> 5) & 0xff;
        var word3 = std.mem.readInt(u32, candidate[12..16], .little);
        const weight = ctx.edge(u32, &.{ 0, 1, 0x20, 0x4b, 0x4c, 0x4c, 0x4d, 0x80, 0xff, ctx.range(u32, 0, 0x60) });
        word3 = (word3 & ~(@as(u32, 0xff) << 19)) | (weight << 19);
        std.mem.writeInt(u32, candidate[12..16], word3, .little);
        weights[i] = weight;
        std.mem.writeInt(u32, state[0x430 + i * 4 ..][0..4], @intCast(@intFromPtr(candidate)), .little);
    }
    // A pointer at 0x470 or after is part of the list; the count lives only as the argument here.
    skipped_calls = if (count > 16) 1 else 0;
    var winner: ?usize = null;
    var i: usize = 0;
    while (i < 24) : (i += 1) {
        const capped: u32 = if (i < 20) @min(weights[i], 0x4c) else 0;
        const base: u32 = switch (ctx.range(u8, 0, 3)) {
            0 => capped -% 1,
            1 => capped,
            2 => capped +% 1,
            else => ctx.int(u32),
        };
        rolls[i] = (ctx.int(u32) << 7) | (base & 0x7f);
    }
    if (count > 0) {
        i = 0;
        while (i < @as(usize, @intCast(count))) : (i += 1) {
            if (actions[i] == 0) continue;
            if ((rolls[i] & 0x7f) < @min(weights[i], 0x4c)) {
                winner = i;
                break;
            }
        }
    }
    which = if (count <= 0) 0 else if (count > 16) 4 else if (winner) |w| (if (w == 0) 2 else 3) else 1;
    return .{ state, count };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        const index = ctx.calls -| skipped_calls; // saturating: a mutation that skips pc_unimplemented has one call fewer
        return @bitCast(rolls[@min(index, rolls.len - 1)]);
    }
};
