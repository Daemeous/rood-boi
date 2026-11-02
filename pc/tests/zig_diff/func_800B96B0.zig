//! zig_diff spec: func_800B96B0 -> func_800B96B0.func_800B96B0 (4A0A8.c).
//! Inputs: a 3-byte buffer in the arena (arg0[1] is the button mask byte, arg0[2]
//! the value stored into the counter), an index 0-3 into D_800F4C58 (the table
//! has 4 entries; the C reads nothing else), and a counter entry at 0, 1, 2, -1,
//! -32768 or 32767 so the decrement hits 0, goes negative, wraps at -32768, or
//! stays positive. The global vs_main_buttonsPressed.all is 0, a sign-bit-only
//! value (which never overlaps the u8 mask), all ones or a low byte, so the
//! button test is true and false both with and without bit 31 set. No callees.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B96B0));

pub const classes = [_][:0]const u8{ "counter-zero", "buttons-pressed", "expires", "counts-down" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(3, 1);
    buf[0] = ctx.int(u8);
    buf[1] = if (ctx.chance(3)) 0 else ctx.int(u8);
    buf[2] = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });

    const idx = ctx.edge(i16, &.{ 0, 1, 2, 3 });
    const tab = v.g.D_800F4C58();
    tab[@as(usize, @intCast(idx))] = ctx.edge(i16, &.{ 0, 1, 2, -1, -32768, 32767 });

    const btn = v.g.vs_main_buttonsPressed();
    btn.all = if (ctx.chance(3)) 0 else ctx.pick(u32, &.{ 0x8000_0000, 0xFFFF_FFFF, 0xFF, 0x0000_0100 });

    return .{ buf, idx };
}

pub fn classify(args: Args) usize {
    const tab = v.g.D_800F4C58();
    const entry = tab[@as(usize, @intCast(args[1]))];
    if (entry == 0) return 0;
    const btn = v.g.vs_main_buttonsPressed();
    if ((btn.all & @as(u32, args[0][1])) != 0) return 1;
    if ((entry -% 1) <= 0) return 2;
    return 3;
}
