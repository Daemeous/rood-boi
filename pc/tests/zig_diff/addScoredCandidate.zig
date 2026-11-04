//! zig_diff spec: func_800DC344 -> pc.candidate_record.addScoredCandidate (D2: pc/src/battle_helpers.c).
//! Inputs: a state block with the entry count at 0x470 at the edges around
//! 16 or INT_MAX (a full list: nothing is added). Negative counts are not
//! driven: the C's record pointer base + 0x2F0 + count * 0x14 then lies below
//! the list and the 64-bit C's psx_addr rejected it (a pointer outside the
//! guest range), and the count is a list size, never negative; a candidate
//! whose weight (bits 19-26 of word 3) is zero or nonzero, and the used-table
//! D_800F5904 pointing at a random-filled 0x1000-byte arena block. The stub
//! of func_800DC2E0 records the record and the candidate.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").addScoredCandidate));

pub const classes = [_][:0]const u8{ "list-full", "no-weight", "added" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x500, 16);
    for (state[0..0x500]) |*byte| byte.* = ctx.int(u8);
    const count = ctx.edge(c_int, &.{ 0, 1, 7, 14, 15, 15, 16, 16, 17, 0x7FFFFFFF });
    std.mem.writeInt(i32, state[0x470..][0..4], count, .little);
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    var word3 = std.mem.readInt(u32, candidate[12..16], .little);
    if (ctx.chance(3)) word3 &= ~(@as(u32, 0xff) << 19);
    if (ctx.chance(4)) word3 = (word3 & ~(@as(u32, 0xff) << 19)) | (@as(u32, 1) << 19);
    std.mem.writeInt(u32, candidate[12..16], word3, .little);
    const used = ctx.allocBytes(0x1000, 16);
    for (used[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F5904().* = @intCast(@intFromPtr(used));
    which = if (count >= 16) 0 else if ((word3 >> 19) & 0xff == 0) 1 else 2;
    return .{ state, @ptrCast(candidate) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
