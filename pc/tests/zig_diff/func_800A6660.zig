//! zig_diff spec: func_800A6660 -> func_800A6660.func_800A6660 (3A1A0.c).
//! Inputs: an arena D_800F4538_t whose facing, unk3E, unk18 and unk63C come
//! from edge sets (unk18 small so unk18 == delta happens, unk63C at 128, unk3E
//! near the +-32 and +-2048 turn edges). Half the runs take directed modes
//! that put arg1 exactly one half turn (+-2048) from the turn's base: the
//! zero-unk6 turn then has delta == +-2048 and the set-turn has delta ==
//! +-2048, so the `delta >= 4096 / 2` and `delta < -4096 / 2` compares see
//! equal sides often. The other runs take a random arg1 (the facing for an
//! early return, a wrap-edge value, a random turn, or a wild value that keeps
//! the C's int arithmetic in range). arg2 has an unk6 that is zero, positive,
//! or negative, and is null only when unk18 is zero (the C never reads it
//! then). The callee writes unk18 or unk3E of the object it was passed,
//! sometimes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A6660));

pub const classes = [_][:0]const u8{ "early-return", "no-turn-call", "turn-unk6-zero", "turn-unk6-set" };

var obj: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800AA850(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = a;
        _ = b;
        _ = c;
        const o = obj orelse return;
        if (ctx.value(u2) == 0) o.unk0.unk18 = ctx.value(u16);
        if (ctx.value(u2) == 1) o.unk0.unk3E = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const o = ctx.alloc(v.D_800F4538_t);
    obj = o;
    o.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 2048, -2048, 4095, 4096, 32767, -32768 });
    o.unk0.unk3E = ctx.pick(i16, &.{ 0, 1, -1, 31, 32, -31, -32, 33, -33, 64, -64, 96, 0x7FFF, -32768, 2048, -2048 });
    o.unk0.unk18 = ctx.pick(u16, &.{ 0, 1, 2, 3, 4, 12, 0x8000, 0xFFFF });
    o.unk63C = ctx.pick(u16, &.{ 0, 127, 128, 129, 0x8000, 0xFFFF });

    const f: i32 = o.unk0.facing;
    const base: i32 = (f + @as(i32, o.unk0.unk3E)) & 0xFFF;
    const a2 = ctx.alloc(v.func_800A6660_t);

    const mode = ctx.range(u8, 0, 3);
    if (mode < 2) {
        // Directed: the turn's delta is exactly one half turn (+-2048).
        // Non-zero unk18 so the turn code runs.
        if (o.unk0.unk18 == 0) o.unk0.unk18 = 1;
        // (and one below each: GCC tests `delta > 2047` and `delta < -2048`)
        const edges = [_]i32{ 2048, -2048, 2047, -2049 };
        const edge: i32 = edges[ctx.range(usize, 0, edges.len - 1)];
        if (mode == 0) {
            // Zero-unk6 turn: delta = arg1 - ((facing + unk3E) & 0xFFF).
            a2.unk6 = 0;
            return .{ o, base + edge, a2 };
        }
        // Set turn: delta = arg1 - facing.
        a2.unk6 = ctx.pick(i16, &.{ 1, -1, 0x7FFF, -32768, 5 });
        return .{ o, f + edge, a2 };
    }

    const arg1: c_int = switch (ctx.range(u8, 0, 9)) {
        0, 1 => f,
        2 => base + 2048,
        3 => base - 2048,
        4 => f + 2048,
        5 => f - 2048,
        6 => f + ctx.range(i32, -5000, 5000),
        7 => ctx.range(i32, -(1 << 30), 1 << 30),
        8 => ctx.pick(i32, &.{ -1, 4095, 4096, 2047, -2049, 65535, -65536 }),
        else => base + ctx.range(i32, -2100, 2100),
    };

    a2.unk6 = ctx.pick(i16, &.{ 0, 0, 1, -1, 0x7FFF, -32768, 5 });
    if (o.unk0.unk18 == 0 and ctx.chance(2)) return .{ o, arg1, null };
    return .{ o, arg1, a2 };
}

pub fn classify(args: Args) usize {
    const o = args[0].?;
    if (args[1] == @as(c_int, o.unk0.facing)) return 0;
    if (o.unk0.unk18 == 0) return 1;
    return if (args[2].?.unk6 == 0) 2 else 3;
}
