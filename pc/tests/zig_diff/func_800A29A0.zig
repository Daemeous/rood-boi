//! zig_diff spec: func_800A29A0 -> func_800A29A0.func_800A29A0 (3A1A0.c).
//! Inputs: an actor and a second D_800F4538_t object in the arena. Every
//! entry of the D_800F4538 table points at one of the two, so the C's reads
//! through the table always hit valid memory; the table indices the C takes
//! (unk12, unk17FD) are kept within 0..16 because the table has 17 entries
//! and the bytes past it are not owned by the harness. The switch selector
//! (unkB_0) covers cases 1-5 and default. The word at offset 8 (unk9_0 and
//! unkB_0 bits), the 0xFC tile flag, the 0x20000 flag at 0x119C, unk5CC and
//! unk17FD are all biased toward the edges the C tests: zero and nonzero
//! masks, bit 9 and bit 8 sides of the `< 9` and `>= 8` tests, and 2 for
//! `>= 2`. The callees write the word at offset 8, the buffer at 0x5EC
//! (the u16 at 0x5F2 that the C copies into facing), unk5CC and unk17FD
//! (with the table index clamped to 0..16), and return random values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A29A0));

pub const classes = [_][:0]const u8{ "case1", "case2", "case3", "case4", "case5", "default" };

/// The actor the callees write back into (set by prepare).
var actor_ptr: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        if (ctx.value(u1) == 1) psx.byteAt(u32, actor_ptr.?, 8).* = ctx.value(u32);
        return ctx.value(c_int);
    }

    pub fn func_800A01C8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        if (ctx.value(u1) == 1) psx.byteAt(u32, actor_ptr.?, 8).* = ctx.value(u32);
        if (ctx.value(u1) == 1) actor_ptr.?.unk5CC = ctx.value(u8);
        return ctx.value(c_int);
    }

    pub fn func_800ACF54(ctx: *zd.Ctx, p: anytype) void {
        _ = p;
        const a = actor_ptr.?;
        if (ctx.value(u1) == 1) psx.byteAt(u32, a, 8).* = ctx.value(u32);
        if (ctx.value(u1) == 1) a.unk17FD = ctx.value(u8) % 17;
    }

    /// Variadic in the C prototype, so the harness passes no arguments beyond ctx.
    pub fn func_800E4C28(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) actor_ptr.?.unk17FD = ctx.value(u8) % 17;
    }

    pub fn func_800A9C54(ctx: *zd.Ctx, p: anytype, buf: anytype, k: anytype) c_uint {
        _ = p;
        _ = k;
        const bytes: [*]u8 = @ptrCast(@alignCast(buf.?));
        @as(*align(1) u16, @ptrCast(bytes + 6)).* = ctx.value(u16);
        return ctx.value(c_uint);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    const o = ctx.alloc(v.D_800F4538_t);
    actor_ptr = a;

    // Every table entry is the actor or the other object.
    for (v.g.D_800F4538()) |*e| e.* = if (ctx.chance(3)) a else o;

    psx.setBits(&a.unk0, "unkB_0", ctx.range(u4, 0, 15));
    psx.setBits(&a.unk0, "unk9_0", ctx.pick(u4, &.{ 0, 7, 8, 9, 15 }));
    a.unk0.unk13 = if (ctx.chance(2)) 0xFC else ctx.int(u8);
    a.unk0.unk12 = @min(ctx.pick(u8, &.{ 0, 1, 2, 8, 16 }), 16);
    a.unk17FD = ctx.pick(u8, &.{ 0, 1, 2, 3, 16 }) % 17;
    a.unk5CC = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });

    psx.setBits(&o.unk0, "unk9_0", ctx.pick(u4, &.{ 0, 7, 8, 9, 15 }));
    psx.setBits(&o.unk0, "unkB_0", ctx.pick(u4, &.{ 0, 0, 5 }));

    return .{a};
}

pub fn classify(args: Args) usize {
    return switch (psx.getBits(&args[0].?.unk0, "unkB_0")) {
        1 => 0,
        2 => 1,
        3 => 2,
        4 => 3,
        5 => 4,
        else => 5,
    };
}
