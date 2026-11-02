//! zig_diff spec: func_800BF5F0 -> func_800BF5F0.func_800BF5F0 (4A0A8.c).
//! Inputs: a 256-byte block in the arena with three u16 header offsets (the
//! first is any u16 and only stored; the two table offsets land in 16..200 so
//! every table read stays inside the block). Each table entry is zero about
//! one time in six, an edge value (0x8000, 0xFFFF, 0x7FFF, 1) one time in
//! eight, and otherwise random, so both sides of every zero test and both
//! signs of every loaded offset occur. The classes are the zero entries found
//! in each table. The callees are recording stubs; setStateFlag may write a
//! global, and getcurrentMusicSlot returns any 32-bit value (stored to a u8).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BF5F0));

pub const classes = [_][:0]const u8{ "no-zero-entries", "zero-in-first", "zero-in-second", "zero-in-both" };

pub const stubs = struct {
    pub fn vs_battle_setStateFlag(ctx: *zd.Ctx, flag: anytype, on: anytype) void {
        _ = flag;
        _ = on;
        if (ctx.value(u1) == 1) v.g.D_800F4BF8().* = ctx.value(u8);
    }
    pub fn vs_main_getcurrentMusicSlot(ctx: *zd.Ctx) c_int {
        return ctx.value(c_int);
    }
};

fn put16(data: [*]u8, off: usize, val: u16) void {
    psx.byteAt(u16, data, @intCast(off)).* = val;
}

fn get16(data: [*]u8, off: usize) u16 {
    return psx.byteAt(u16, data, @intCast(off)).*;
}

fn entry(ctx: *zd.Ctx) u16 {
    if (ctx.chance(6)) return 0;
    if (ctx.chance(8)) return ctx.pick(u16, &.{ 1, 0x8000, 0xFFFF, 0x7FFF });
    return ctx.int(u16);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const data = ctx.allocBytes(256, 16);
    const t1: usize = ctx.range(u16, 16, 200);
    const t2: usize = ctx.range(u16, 16, 200);
    put16(data, 2, ctx.int(u16));
    put16(data, 4, @intCast(t1));
    put16(data, 6, @intCast(t2));
    for (0..8) |k| {
        put16(data, t1 + 2 * k, entry(ctx));
        put16(data, t2 + 2 * k, entry(ctx));
    }
    return .{data};
}

pub fn classify(args: Args) usize {
    const d: [*]u8 = args[0];
    const t1: usize = get16(d, 4);
    const t2: usize = get16(d, 6);
    var zero1 = false;
    var zero2 = false;
    for (1..8) |k| {
        if (get16(d, t1 + 2 * k) == 0) zero1 = true;
    }
    for (0..8) |k| {
        if (get16(d, t2 + 2 * k) == 0) zero2 = true;
    }
    var cls: usize = 0;
    if (zero1) cls += 1;
    if (zero2) cls += 2;
    return cls;
}
