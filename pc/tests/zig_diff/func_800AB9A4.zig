//! zig_diff spec: func_800AB9A4 -> func_800AB9A4.func_800AB9A4 (40564.c).
//! Inputs: a random D_800F45E0_t whose flag word (+8) takes every mode nibble
//! 0-15, with the 0x80 and 0x70000 bits, the 0x5AC flags and the 0xA word set
//! at random. unkF is 0-9 (so the weapon table index stays in range), unk13
//! is 0-15 (the object table). prepare fills the 16-entry object table and the
//! 20-entry weapon table (some null), and picks the tick speed near the
//! halving and the 0x40/0x41 clamp, with the unk54, unk6F1, unk6F2 and unk57
//! bytes biased onto the values that make each ordered test equal. The
//! stubs write what the real callees can change (unk34, unk38, unk1C, the
//! 0x6F2 and 0xA words, the out-pointers, the SVECTOR vy) and return zero or
//! non-zero as the C tests them.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB9A4));

pub const classes = [_][:0]const u8{ "case2", "case3", "case4", "case6", "case7", "case9", "case10", "case11", "case12-14", "default" };

/// A field of an object at a byte offset (unaligned view).
fn fieldAt(comptime T: type, p: anytype, off: usize) *align(1) T {
    const base: usize = if (@typeInfo(@TypeOf(p)) == .optional) @intFromPtr(p.?) else @intFromPtr(p);
    return @ptrFromInt(base + off);
}

fn put(comptime T: type, p: anytype, off: usize, val: T) void {
    fieldAt(T, p, off).* = val;
}

fn asU8(x: i32) u8 {
    return @truncate(@as(u32, @bitCast(x)));
}

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(i16);
    }

    pub fn func_800A70DC(ctx: *zd.Ctx, obj: anytype, n: anytype) void {
        _ = n;
        if (ctx.value(u1) == 1) fieldAt(i16, obj, 0x1C).* = ctx.value(i16);
    }

    pub fn func_800A7524(ctx: *zd.Ctx, obj: anytype, field: anytype) void {
        fieldAt(i16, field, 0).* = ctx.value(i16);
        if (ctx.value(u1) == 1) fieldAt(i16, obj, 0x38).* = ctx.value(i16);
    }

    pub fn func_800AC690(ctx: *zd.Ctx, bank: anytype, obj: anytype) void {
        _ = bank;
        if (ctx.value(u2) == 0) fieldAt(u8, obj, 0x6F2).* = ctx.value(u8);
        if (ctx.value(u2) == 0) fieldAt(u16, obj, 0xA).* = ctx.value(u16);
    }

    pub fn func_800AD494(ctx: *zd.Ctx, obj: anytype, id: anytype, out: anytype) c_int {
        _ = obj;
        _ = id;
        if (ctx.value(u1) == 0) {
            out.* = null;
        } else {
            const p: [*]align(2) u8 = @alignCast(ctx.allocBytes(16, 2));
            out.* = @ptrCast(p);
        }
        return ctx.value(c_int);
    }

    pub fn func_800A190C(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype, n: anytype) void {
        _ = a;
        _ = b;
        _ = n;
        out.*.vy = ctx.value(i16);
    }

    pub fn func_800AC0D4(ctx: *zd.Ctx, a: anytype, b: anytype, n: anytype) c_int {
        _ = b;
        _ = n;
        if (ctx.value(u1) == 1) fieldAt(u8, a, 0).* = ctx.value(u8);
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800AC168(ctx: *zd.Ctx, colors: anytype, count: anytype, amount: anytype, mode: anytype, reference: anytype) c_int {
        _ = colors;
        _ = count;
        _ = amount;
        _ = mode;
        _ = reference;
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F45E0_t);
    const pool_a = ctx.alloc(v.D_800F45E0_t);
    const pool_b = ctx.alloc(v.D_800F45E0_t);
    const weapons = [3]*v.vs_battle_wepModels_t{
        ctx.alloc(v.vs_battle_wepModels_t),
        ctx.alloc(v.vs_battle_wepModels_t),
        ctx.alloc(v.vs_battle_wepModels_t),
    };

    // The object table: the C reads D_800F45E0[unk13] for unk13 in 0..15.
    const table = v.g.D_800F45E0();
    for (table, 0..) |*slot, i| {
        slot.* = if (ctx.chance(8)) arg else if (i % 2 == 0) pool_a else pool_b;
    }

    // The weapon table: 20 entries, the C indexes unkF*2+0/1 with unkF 0-9.
    const wt = v.g.vs_battle_wepModels();
    for (wt, 0..) |*slot, i| {
        slot.* = if (ctx.chance(4)) null else weapons[i % 3];
    }

    // Tick speed around the halving edges.
    const ts = ctx.edge(i32, &.{ 0, 1, 2, 3, 4, -1, -2, -3, -4, 7, -7, 9, 100, -100, 1000, -1000 });
    v.g.vs_gametime_tickspeed().* = ts;
    const s4: i32 = (ts + @as(i32, @intFromBool(ts < 0))) >> 1;

    // Flag word at +8: the mode nibble (bits 8-11) chosen uniformly, bit 7 and
    // the 0x70000 bits chosen, the sign bit random.
    const mode: u32 = ctx.range(u8, 0, 15);
    var w8: u32 = ctx.int(u32);
    w8 = (w8 & ~@as(u32, 0xF00)) | (mode << 8);
    if (ctx.chance(2)) {
        w8 |= 0x80;
    } else {
        w8 &= ~@as(u32, 0x80);
    }
    if (ctx.chance(3)) {
        w8 &= ~@as(u32, 0x70000);
    } else {
        const sh: u5 = @intCast(ctx.range(u8, 0, 2));
        w8 |= @as(u32, 0x10000) << sh;
    }
    put(u32, arg, 8, w8);

    // Flags at 0x5AC: bit 3 (the C's "& 8") and bit 27 each half the time.
    var w5: u32 = ctx.int(u32);
    if (ctx.chance(2)) {
        w5 |= 8;
    } else {
        w5 &= ~@as(u32, 8);
    }
    if (ctx.chance(2)) {
        w5 |= 0x0800_0000;
    } else {
        w5 &= ~@as(u32, 0x0800_0000);
    }
    put(u32, arg, 0x5AC, w5);

    put(u16, arg, 0xA, ctx.int(u16));
    put(u8, arg, 0xF, ctx.edge(u8, &.{ 0, 0, 1, 2, 5, 9 }));
    put(u8, arg, 0x13, ctx.range(u8, 0, 15));
    put(u16, arg, 0x63C, if (ctx.chance(4)) 0 else ctx.int(u16));

    // unk54 at the value 4*s4 (so var_v1 == 0) or at an edge.
    put(u8, arg, 0x54, if (ctx.chance(3)) asU8(4 * s4) else ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 8, 64, 255 }));

    // unk57: 14 and 13 come up often so the "< 15" test sees both sides.
    const r57 = ctx.range(u8, 0, 7);
    const b57: u8 = switch (r57) {
        0, 1 => 14,
        2 => 13,
        3 => 15,
        else => ctx.int(u8),
    };
    put(u8, arg, 0x57, b57);

    // unk6F2 so that (signed char)unk6F2 + 2*s4 sits on the 0x40/0x41 edges;
    // unk6F1 equal to the value after the subtraction half the time.
    const want: i32 = ctx.edge(i32, &.{ 0x40, 0x41, 0x3F, 0x42, 0, -1, 0x7F, -128 });
    const f2: u8 = if (ctx.chance(2)) asU8(want - 2 * s4) else ctx.int(u8);
    put(u8, arg, 0x6F2, f2);
    const f2s: i32 = @as(i8, @bitCast(f2));
    const f1: u8 = if (ctx.chance(3)) asU8(f2s - 2 * s4) else ctx.int(u8);
    put(u8, arg, 0x6F1, f1);

    return .{arg};
}

pub fn classify(args: Args) usize {
    const mode = (fieldAt(u32, args[0].?, 8).* >> 8) & 0xF;
    return switch (mode) {
        2 => 0,
        3 => 1,
        4 => 2,
        6 => 3,
        7 => 4,
        9 => 5,
        10 => 6,
        11 => 7,
        12, 14 => 8,
        else => 9,
    };
}
