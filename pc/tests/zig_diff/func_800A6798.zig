//! zig_diff spec: func_800A6798 -> func_800A6798.func_800A6798 (3A1A0.c).
//! Inputs: a D_800F4538_t, an offset and a motion vector in the arena, with the
//! globals D_800F4B0A, D_800F49F9 and D_800F4B08 left as random bytes. The
//! unk6EC limit is hit exactly a quarter of the time, unk63C and offset->vy get
//! edge values (0, 4095/4096, 0x8000, 0xFFFF, the i16 wrap of vy + 0xEC). The
//! callees are recording stubs: func_800A76BC writes its height out-parameter
//! (5 and 4 and 6 among random values), sometimes flips the 0x5AC bit the C
//! re-reads, and returns 3 a quarter of the time. func_800A6EE8 writes the
//! global D_800F4B08.vy and returns vy - 63 (the C's exact -0x3F edge) or
//! vy - 64 and random values. rsin returns a random i16.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A6798));

pub const classes = [_][:0]const u8{ "early-return", "blocked-5AC", "vz-axis", "vx-axis" };

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800A76BC(ctx: *zd.Ctx, actor: anytype, motion: anytype, height: anytype, flag: anytype) c_int {
        _ = motion;
        _ = flag;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        const h: *c_int = @ptrCast(@alignCast(height));
        h.* = switch (ctx.value(u3)) {
            0 => 5,
            1 => 4,
            2 => 6,
            else => @as(c_int, ctx.value(i16)),
        };
        if (ctx.value(u2) == 0) psx.setBits(a, "unk5AC_13", ctx.value(u1));
        if (ctx.value(u2) == 0) return 3;
        return @as(c_int, ctx.value(i8));
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, x: anytype, z: anytype, f: anytype) c_int {
        _ = p;
        _ = x;
        _ = z;
        _ = f;
        const gb = v.g.D_800F4B08();
        if (ctx.value(u2) == 0) gb.vy = ctx.value(i16);
        return switch (ctx.value(u2)) {
            0 => @as(c_int, gb.vy) - 63,
            1 => @as(c_int, gb.vy) - 64,
            else => @as(c_int, ctx.value(i16)),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const offset = ctx.alloc(v.SVECTOR);
    const motion = ctx.alloc(v.SVECTOR);
    // The early return: unk6EC equal to the limit a quarter of the time.
    if (ctx.chance(4)) actor.unk6EC = v.g.D_800F4B0A().*;
    actor.unk63C = ctx.pick(u16, &.{ 0, 1, 4095, 4096, 0x8000, 0xFFFF });
    offset.vy = ctx.pick(i16, &.{ 0, -236, 32767, -32768 });
    offset.vx = ctx.int(i16);
    offset.vz = ctx.int(i16);
    return .{ actor, offset, motion };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (actor.unk6EC <= v.g.D_800F4B0A().*) return 0;
    if (psx.getBits(actor, "unk5AC_13") == 1) return 1;
    if (((v.g.D_800F49F9().* >> 1) & 1) == 1) return 2;
    return 3;
}
