//! zig_diff spec: func_800AA984 -> func_800AA984.func_800AA984 (40564.c).
//! Inputs: an index 0-16 into D_800F4538 whose entry is an arena actor (random
//! bytes, a facing at the wrap and sign edges, a speed factor unk5C0 at the
//! 4096 split points) or null; arg1 at its edges (0, +-1, 4096, the short
//! extremes); arg2 at 0, -1 and other values. Covers the null return, the
//! direct store, the recentre with a wrapping and a negative sum, and the
//! speed path: zero speed (falls into the recentre), a partial unit (unk18 = 1),
//! and speed of 4096 or more (whole or rounded up).
//! The sign bit of speed is unreachable: |m| <= 32768 and unk5C0 is a u16, so
//! speed < 2^31 and `speed & 0xFFF` only sees nonnegative values.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA984));

pub const classes = [_][:0]const u8{
    "null-actor",
    "direct-store",
    "recentre",
    "speed-zero",
    "speed-partial",
    "speed-4096-or-more",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = ctx.range(c_int, 0, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    actor.unk5C0 = ctx.pick(u16, &.{ 0, 1, 255, 256, 4095, 4096, 4097, 8192, 65535 });
    actor.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, -4096, 32767, -32768 });
    const table = v.g.D_800F4538();
    table[@intCast(idx)] = if (ctx.chance(5)) null else actor;
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 255, 4096, -4096, 32767, -32768 });
    const arg2 = ctx.pick(c_int, &.{ 0, -1, 1, 2, 4096, -2, 0x7FFF_FFFF, -2147483648 });
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const a = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    if (args[2] == 0) return 2;
    if (args[2] != -1) return 1;
    var m: i32 = args[1];
    if (m < 0) m = -m;
    const speed: i32 = m * @as(i32, a.unk5C0);
    if (speed == 0) return 3;
    return if (speed < 4096) 4 else 5;
}
