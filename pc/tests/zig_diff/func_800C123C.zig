//! zig_diff spec: func_800C123C -> func_800C123C.func_800C123C (58578.c).
//! Inputs: a random func_800C1564_t as arg0 (never null: the C dereferences it
//! with no null test), its unk1 byte zero, small or large; three random u16
//! samples with edge values; arg2 zero, negative or positive. func_800C0FA8
//! fills the unk10 and unk18 tables (unk18 sometimes small, so the squared
//! sums stay small and return 1 is common) and sometimes rewrites unk1.
//! ApplyMatrixSV copies the input vx and vz, so the loop's arithmetic reaches
//! the result. In "equal" runs it makes the vy the check compares equal to
//! unk1*32, and the squared sums equal the squared radius (both 0). A zero
//! unk1 never meets vy == 0, so the C's division does not trap.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C123C));

pub const classes = [_][:0]const u8{ "u1-zero", "eq-sides", "arg2-set", "other" };

// Set by prepare, read by the stubs (the same in both runs).
var g_obj: ?*v.func_800C1564_t = null;
var g_eq: bool = false;
var g_arg2: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C1564_t);
    g_obj = obj;
    obj.unk4.values[1] = ctx.pick(u8, &.{ 0, 1, 2, 127, 128, 255 });
    g_eq = ctx.chance(3);

    const buf = ctx.alloc([3]u16);
    for (buf) |*e| e.* = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });

    const arg2: c_int = ctx.pick(c_int, &.{ 0, 1, -1, std.math.minInt(c_int) });
    g_arg2 = arg2;

    const arg1: [*c]c_ushort = @ptrCast(&buf[0]);
    return .{ obj, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const o = args[0].?;
    if (o.unk4.values[1] == 0) return 0;
    if (g_eq) return 1;
    if (args[2] != 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_800C0FA8(ctx: *zd.Ctx, arg0: anytype, work: anytype, matrix: anytype) void {
        _ = matrix;
        const o: *v.func_800C1564_t = @ptrCast(@alignCast(arg0));
        const w: *v.func_800C0FA8_t2 = @ptrCast(@alignCast(work));
        // The callee may rewrite the unk1 byte the C reads back after the call.
        if (ctx.value(u1) == 1) o.unk4.values[1] = ctx.value(u8);
        // The tables the C reads back; the C's loop and the check depend on them.
        const small = ctx.value(u1) == 1;
        for (&w.unk10) |*e| e.* = ctx.value(i16);
        for (&w.unk18) |*e| {
            const x = ctx.value(i16);
            e.* = if (small) x >> 12 else x;
        }
    }

    pub fn ApplyMatrixSV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) [*c]v.SVECTOR {
        _ = m;
        const o = g_obj.?;
        const unit: i16 = o.unk4.values[1];
        const src: *v.SVECTOR = @ptrCast(@alignCast(v0));
        const dst: *v.SVECTOR = @ptrCast(@alignCast(v1));
        dst.vx = src.vx;
        dst.vz = src.vz;
        dst.vy = ctx.value(i16);
        if (g_eq) {
            // The check's sides become equal: -vy == unk1*32 after the
            // C's optional subtraction, and the squared sums are 0 == 0.
            dst.vx = 0;
            dst.vz = 0;
            dst.vy = if (g_arg2 != 0) 0 else -(unit * 32);
        }
        // With unk1 zero the C divides by unk1: vy must not be zero.
        if (dst.vy == 0 and unit == 0) dst.vy = 1;
        return null;
    }
};
