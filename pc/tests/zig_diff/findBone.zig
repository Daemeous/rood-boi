//! zig_diff spec: func_800A152C -> find_bone.findBone (38C1C.c).
//! Inputs: arg0 in 0..16 selects a D_800F4538 entry (an arena D_800F4538_t
//! object), a D_800F45E0 fallback entry (the same object type, for when the
//! first is null), or neither (-1). The object's unk68 points at an arena
//! block of armatures; nBones runs 0..255 and the slot one past nBones is
//! filled with matching values so the `<=` mutation is visible. arg1 covers
//! 0xFF, -1 (masked to 0xFF but not equal), the low-byte edges and random
//! high bits; arg2 covers 0..3 and values that hit no case. The `new_var`
//! test sees only 0 and 1, since the C stores a comparison result.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").findBone));

pub const classes = [_][:0]const u8{
    "no-object",
    "exact-ff",
    "masked-ff",
    "bad-case",
    "no-bones",
    "fallback-table",
    "searched",
};

/// The class prepare chose, returned by classify (called after prepare).
var last_class: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const blk = ctx.allocBytes(64 + 16 * 256, 16);
    psx.put(&obj.unk0.unk68, @as(*v.D_800F4538_unk68, @ptrCast(@alignCast(blk))));

    const n: u8 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 255 });
    obj.unk0.nBones = n;

    // arg1: its low byte is the target; high bits and the 0xFF forms vary.
    const low = ctx.pick(u8, &.{ 0, 1, 14, 15, 16, 0x7F, 0x80, 0xFE, 0xFF });
    const high: u32 = if (ctx.chance(2)) ctx.int(u32) & 0xFFFF_FF00 else 0;
    var arg1: c_int = @bitCast(high | @as(u32, low));
    if (ctx.chance(8)) arg1 = 0xFF;
    if (ctx.chance(12)) arg1 = -1;
    const t: u8 = @truncate(@as(u32, @bitCast(arg1)));

    const arg2: c_int = if (ctx.chance(5))
        ctx.pick(c_int, &.{ -1, 4, 255, -2147483648 })
    else
        ctx.range(c_int, 0, 3);

    // Armatures 0..n: the slot at n is one past nBones and must be matched
    // sometimes so an off-by-one bound is seen.
    const nib: u8 = if (t < 15) t + 1 else ctx.int(u8) & 0xF;
    var i: usize = 0;
    while (i <= n) : (i += 1) {
        const a = psx.byteAt(v._armature_t, blk, @intCast(64 + 16 * i));
        a.unk6 = if (ctx.chance(3)) t else ctx.pick(u8, &.{ 0, 0xFF, 0x7F, 0x80 });
        a.unk7 = switch (ctx.range(u8, 0, 3)) {
            0 => (nib << 4) | (ctx.int(u8) & 0xF),
            1 => (ctx.int(u8) & 0xF0) | nib,
            else => ctx.int(u8),
        };
        a.unk8 = if (ctx.chance(3)) t else ctx.int(u8);
    }

    // Table entries: the one arg0 selects.
    const g4538 = v.g.D_800F4538();
    const g45e0 = v.g.D_800F45E0();
    const mode = ctx.range(u8, 0, 99);
    var idx: usize = ctx.range(usize, 0, 16);
    var no_object = false;
    var fallback = false;
    if (mode < 10) {
        idx = ctx.range(usize, 0, 15);
        g4538[idx] = null;
        g45e0[idx] = null;
        no_object = true;
    } else if (mode < 30 and idx < 16) {
        g4538[idx] = null;
        g45e0[idx] = @as(*v.D_800F45E0_t, @ptrCast(obj));
        fallback = true;
    } else {
        g4538[idx] = obj;
    }

    last_class = if (no_object)
        0
    else if (arg1 == 0xFF)
        1
    else if ((arg1 & 0xFF) == 0xFF)
        2
    else if (arg2 < 0 or arg2 > 3)
        3
    else if (n == 0)
        4
    else if (fallback)
        5
    else
        6;

    return .{ @as(c_int, @intCast(idx)), arg1, arg2 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return last_class;
}
