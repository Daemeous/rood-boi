//! zig_diff spec: func_800DFA54 -> func_800DFA54.func_800DFA54 (771A8.c).
//! Inputs: D_800F5910 is pointed at a 128-entry block the spec allocates, with
//! 32 entries before the pointer's target so the C's negative indices are
//! readable. arg0 is -4..8, so the scan reads entries 4*arg0 .. 4*arg0+3.
//! arg1 and arg2 are built from the bitfields of the scanned entries (the
//! first pair, the second pair, one field of a pair, the entry after the
//! scan, or random values), so the first and second scan hits, the
//! fall-through to the default index, and both stores are all taken.
//! Random arg1/arg2 values cover negative shifts and out-of-range fields.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DFA54));

pub const classes = [_][:0]const u8{ "found-first", "found-second", "not-found" };

/// A shifted-back argument whose >>19 is `bits`, with random low bits.
fn mkArg(ctx: *zd.Ctx, bits: anytype) c_int {
    const low: u19 = ctx.int(u19);
    return (@as(c_int, bits) << 19) | @as(c_int, low);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const raw: [*]v.D_800F5910_t = @ptrCast(@alignCast(ctx.allocBytes(256, 16)));
    const base: [*]v.D_800F5910_t = raw + 32;
    v.g.D_800F5910().* = &base[0];

    const a0 = ctx.range(c_int, -4, 8);
    const first: isize = @as(isize, a0) * 4 + 1;
    const second: isize = first + 2;
    const arg3 = ctx.alloc(v.func_800DFA54_t);

    var arg1: c_int = ctx.int(c_int);
    var arg2: c_int = ctx.int(c_int);
    const mode = ctx.range(u8, 0, 11);
    const k: ?isize = switch (mode) {
        0, 1, 4, 6 => first,
        2, 3, 5 => second,
        7 => first + 4,
        else => null,
    };
    if (k) |kk| {
        const e = psx.at(base, kk);
        if (mode != 6) arg1 = mkArg(ctx, psx.getBits(e, "unk0_0"));
        if (mode != 4 and mode != 5) arg2 = mkArg(ctx, psx.getBits(e, "unk0_5"));
    }
    return .{ a0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    const arg1 = args[1] >> 19;
    const arg2 = args[2] >> 19;
    const base = v.g.D_800F5910().*.?;
    var i: c_int = arg0 * 2;
    while (i < arg0 * 2 + 2) : (i += 1) {
        const e = psx.at(base, i * 2 + 1);
        if (arg1 == @as(c_int, psx.getBits(e, "unk0_0")) and
            arg2 == @as(c_int, psx.getBits(e, "unk0_5")))
        {
            return if (i == arg0 * 2) 0 else 1;
        }
    }
    return 2;
}
