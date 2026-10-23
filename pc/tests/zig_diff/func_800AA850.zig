//! zig_diff spec: func_800AA850 -> func_800AA850.func_800AA850 (40564.c).
//! Inputs: a table index 0-16 (both ends included) whose entry is null or an
//! arena block with a random facing. arg1 is often facing + 2048 or
//! facing - 2048 (mod 2^16), so the two ordered tests see EQUAL sides after
//! the C's i16 truncation of arg1 - facing; otherwise arg1 is a random or
//! edge u16 (wrapping cases included). arg2 is any i32. The callee
//! func_800AA984 is a recording stub: the C reads nothing back after the
//! call, so it writes nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA850));

pub const classes = [_][:0]const u8{ "null-entry", "equal-high", "equal-low", "over-half", "under-half", "middle" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const facing = ctx.pick(i16, &.{ 0, 1, -1, 2047, 2048, -2047, -2048, 2049, -2049, 4096, -4096, 32767, -32768 });
    actor.unk0.facing = facing;
    const f: u16 = @bitCast(facing);
    const a1: u16 = switch (ctx.range(u8, 0, 9)) {
        0, 1, 2 => f +% 2048,
        3, 4, 5 => f -% 2048,
        6 => ctx.edge(u16, &.{ 0, 65535, 2048, 32768 }),
        else => ctx.int(u16),
    };
    const arg1: c_ushort = @bitCast(a1);
    const idx = ctx.range(i32, 0, 16);
    const arr = v.g.D_800F4538();
    arr[@intCast(idx)] = if (ctx.chance(8)) null else actor;
    return .{ idx, arg1, ctx.int(i32) };
}

pub fn classify(args: Args) usize {
    const arr = v.g.D_800F4538();
    const entry = arr[@intCast(args[0])] orelse return 0;
    const t: i16 = @truncate(@as(i32, args[1]) - @as(i32, entry.unk0.facing));
    if (t == 2048) return 1;
    if (t == -2048) return 2;
    if (t > 2048) return 3;
    if (t < -2048) return 4;
    return 5;
}
