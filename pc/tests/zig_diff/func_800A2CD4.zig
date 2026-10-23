//! zig_diff spec: func_800A2CD4 -> func_800A2CD4.func_800A2CD4 (3A1A0.c).
//! Inputs: one D_800F4538_t in the arena with every state (bits 28-30 of the
//! word at +8, the unkB_4 bitfield) from 0 to 7, unk1A zero and nonzero,
//! unk5CA zero and nonzero, the word at 0x119C with and without 0x20000 (and
//! its sign bit), animationId at 0xC7, 0xC9 and others, unk5CC zero and
//! nonzero, and the global D_800F49F4 zero, nonzero in its low half only, and
//! with the sign bit. The GTE-free callees are recording stubs; the ones that
//! change the object or the global (func_800A2FBC, func_800A9EB4, func_800A9378,
//! func_800ACF54) write what the C reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A2CD4));

pub const classes = [_][:0]const u8{
    "state-0-or-7",
    "case1-unk1A-nonzero",
    "case1-unk1A-zero",
    "case2",
    "case3-taken",
    "case3-not-taken",
    "case4",
    "case5",
    "case6-goto",
    "case6-break",
};

/// The object the stubs reach through func_800A9EB4, which gets no pointer.
var obj: ?*v.D_800F4538_t = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    obj = a;
    psx.setBits(&a.unk0, "unkB_4", ctx.range(u3, 0, 7));
    a.unk0.unk1A = if (ctx.chance(2)) 0 else ctx.pick(u16, &.{ 1, 0x8000, 0xFFFF });
    a.unk5CA = ctx.pick(u16, &.{ 0, 1, 0xFFFF });
    a.unk5CC = if (ctx.chance(2)) 0 else 1;
    a.animationId = ctx.pick(u8, &.{ 0xC7, 0xC9, 0xC8, 0 });
    const bits = ctx.int(u32);
    psx.byteAt(u32, a, 0x119C).* = if (ctx.chance(2)) bits | 0x20000 else bits & ~@as(u32, 0x20000);
    psx.global(u32, "D_800F49F4").* = if (ctx.chance(2))
        0
    else
        ctx.pick(u32, &.{ 1, 0x10000, 0x8000_0000, 0xFFFF_0000, 0xFFFF_FFFF });
    return .{a};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const st = psx.getBits(&a.unk0, "unkB_4");
    const w = psx.byteAt(u32, a, 0x119C).*;
    return switch (st) {
        0, 7 => 0,
        1 => if (a.unk0.unk1A != 0) 1 else 2,
        2 => 3,
        3 => if (a.unk0.unk1A == 0 and (w & 0x20000) != 0) 4 else 5,
        4 => 6,
        5 => 7,
        6 => if (a.animationId != 0xC7 and a.animationId != 0xC9 and a.unk5CC == 0) 8 else 9,
    };
}

pub const stubs = struct {
    /// Writes unk5CA (read back by cases 2 and 5), sometimes zero.
    pub fn func_800A2FBC(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) {
            a.unk5CA = if (ctx.value(u1) == 1) 0 else ctx.value(u16);
        }
    }

    /// Writes unk1A of the object (read back by case 4); it gets no pointer,
    /// so it reaches the object through the spec's `obj`.
    pub fn func_800A9EB4(ctx: *zd.Ctx, id: anytype, off: anytype, n: anytype) void {
        _ = id;
        _ = off;
        _ = n;
        if (ctx.value(u1) == 1) {
            if (obj) |a| {
                a.unk0.unk1A = ctx.value(u16);
            }
        }
    }

    /// Writes D_800F49F4 (tested by cases 2 and 5), sometimes zero.
    pub fn func_800A9378(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype, n: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        _ = n;
        if (ctx.value(u1) == 1) {
            psx.global(u32, "D_800F49F4").* = if (ctx.value(u1) == 1) 0 else ctx.value(u32);
        }
        return ctx.value(c_int);
    }

    /// Writes the tile position the C passes on to func_800E4C28.
    pub fn func_800ACF54(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) {
            a.unk0.currentTileX = ctx.value(u8);
            a.unk0.currentTileZ = ctx.value(u8);
        }
    }
};
