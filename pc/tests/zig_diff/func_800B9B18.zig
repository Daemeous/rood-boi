//! zig_diff spec: func_800B9B18 -> func_800B9B18.func_800B9B18 (4A0A8.c).
//! Inputs: a two-byte block in the arena whose bytes are random, so byte 1
//! (the selector byte) takes every value 0-255 and each of its three 2-bit
//! fields takes 0, 1, 2 and 3. Fields 2 and 3 are the switch's default arm.
//! arg1 is a random i16 (the C never reads it). The callees take no arguments
//! and are recording stubs. The classes are the 27 combinations of the three
//! selectors, each reduced to 0, 1 or "other" (2 or 3).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9B18));

pub const classes = blk: {
    var out: [27][:0]const u8 = undefined;
    var i: usize = 0;
    for (0..3) |a| for (0..3) |b| for (0..3) |d| {
        out[i] = std.fmt.comptimePrint("s1:{d} s2:{d} s3:{d} (2: 2 or 3)", .{ a, b, d });
        i += 1;
    };
    break :blk out;
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(2, 1);
    bytes[0] = ctx.int(u8);
    bytes[1] = ctx.int(u8);
    return .{ bytes, ctx.int(i16) };
}

fn sel(x: u8, shift: u3) usize {
    const s = (x >> shift) & 3;
    return if (s > 1) 2 else s;
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    return sel(b, 0) * 9 + sel(b, 2) * 3 + sel(b, 4);
}
