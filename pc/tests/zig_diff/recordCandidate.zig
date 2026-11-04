//! zig_diff spec: func_800DC4F0 -> pc.candidate_record.recordCandidate (D2: pc/src/battle_helpers.c).
//! Inputs: a candidate (random words; word 1's bits 0-4 pick the slot bit)
//! and an info block whose halfword at 0xE has the flag 0x40 set or clear;
//! the slot mask D_800F58F4 with the candidate's bit set or clear (the
//! other bits random); the entry count D_800F58F0 at the edges around 16 and
//! below zero (the list base D_800F58F8 points into the middle of an arena
//! block, so a few negative entries stay inside it) or any int; the stub of
//! func_800DC2E0 records the destination and the candidate.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").recordCandidate));

pub const classes = [_][:0]const u8{ "flag-clear", "slot-used", "list-full", "recorded" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    const info = ctx.allocBytes(0x20, 16);
    for (info[0..0x20]) |*byte| byte.* = ctx.int(u8);
    const flag: u16 = 0x40;
    const halfword = std.mem.readInt(u16, info[0xe..][0..2], .little);
    std.mem.writeInt(u16, info[0xe..][0..2], if (ctx.chance(4)) halfword & ~flag else halfword | flag, .little);
    const word = std.mem.readInt(u32, candidate[4..8], .little);
    const bit = @as(u32, 1) << @intCast((word & 7) * 4 + ((word >> 3) & 3));
    const mask = ctx.int(u32);
    v.g.D_800F58F4().* = if (ctx.chance(3)) mask | bit else mask & ~bit;
    v.g.D_800F58F0().* = ctx.pick(c_int, &.{ 0, 1, 5, 14, 15, 15, 16, 16, 17, -1, -2, 0x7FFFFFFF, -0x80000000 });
    const list = ctx.allocBytes(0x400, 16);
    for (list[0..0x400]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58F8().* = @intCast(@intFromPtr(list) + 0x200);
    which = if (std.mem.readInt(u16, info[0xe..][0..2], .little) & 0x40 == 0)
        0
    else if (v.g.D_800F58F4().* & bit != 0)
        1
    else if (v.g.D_800F58F0().* >= 16)
        2
    else
        3;
    return .{ @ptrCast(candidate), info };
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
