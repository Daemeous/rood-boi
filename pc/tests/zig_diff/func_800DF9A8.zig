//! zig_diff spec: func_800DF9A8 -> func_800DF9A8.func_800DF9A8 (771A8.c).
//! Inputs: arg0 with unk0/unk2 mostly 5-bit values (so they can match a
//! table entry), sometimes the same object as arg1. A 16-entry table, which
//! the D_800F5910 global points at, has its even entries (2i) filled at
//! random, with partial matches on unk0_0 or unk0_5 alone, and with one
//! entry at index 2m matching arg0 in some runs. Matches at every i in 0..7
//! give both the i++ path (even i) and the i-- path (odd i), and the no-match
//! runs take the fallback copy.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DF9A8));

pub const classes = [_][:0]const u8{ "no-match", "match-even", "match-odd", "alias" };

// The table prepare built, for classify (set before classify runs).
var tabp: *[16]v.D_800F5910_t = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.alloc(v.func_800DFA54_t);
    const a1 = if (ctx.chance(8)) a0 else ctx.alloc(v.func_800DFA54_t);

    a0.unk0 = if (ctx.chance(4)) ctx.pick(u8, &.{ 0, 31, 32, 255 }) else ctx.range(u8, 0, 31);
    a0.unk2 = if (ctx.chance(4)) ctx.pick(u8, &.{ 0, 31, 32, 255 }) else ctx.range(u8, 0, 31);
    a0.unk1 = ctx.int(u8);

    // A 16-entry table (the C reads entries 0..15 at 2 bytes each).
    const tab = ctx.alloc([16]v.D_800F5910_t);
    tabp = tab;
    v.g.D_800F5910().* = &tab[0];
    for (tab) |*e| {
        psx.setBits(e, "unk0_0", ctx.range(u5, 0, 31));
        psx.setBits(e, "unk0_5", ctx.range(u5, 0, 31));
    }

    // Partial matches on the even entries the loop reads.
    var k: usize = 0;
    while (k < 8) : (k += 1) {
        const e = &tab[k * 2];
        if (ctx.chance(3)) psx.setBits(e, "unk0_0", @truncate(a0.unk0));
        if (ctx.chance(3)) psx.setBits(e, "unk0_5", @truncate(a0.unk2));
    }

    // One full match at entry 2m in most runs; none in the rest.
    if (!ctx.chance(4)) {
        const m = ctx.range(usize, 0, 7);
        psx.setBits(&tab[m * 2], "unk0_0", @truncate(a0.unk0));
        psx.setBits(&tab[m * 2], "unk0_5", @truncate(a0.unk2));
    }

    return .{ a0, a1 };
}

pub fn classify(args: Args) usize {
    const a0 = args[0].?;
    if (args[0] == args[1]) return 3;
    var i: usize = 0;
    while (i < 8) : (i += 1) {
        const e = &tabp[i * 2];
        if (psx.getBits(e, "unk0_0") == a0.unk0 and psx.getBits(e, "unk0_5") == a0.unk2) {
            return if ((i & 1) == 0) 1 else 2;
        }
    }
    return 0;
}
