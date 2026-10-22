//! zig_diff spec: func_800C0FA8 -> func_800C0FA8.func_800C0FA8 (58578.c).
//! Inputs: a func_800C1564_t and a func_800C0FA8_t2, either separate or aliased
//! (arg1 starts at arg0's first byte, so its stores overwrite arg0's unk0..unk7
//! only after the loop has read the values it needs), and a MATRIX for
//! RotMatrix_gte. The divisors values[0..2] are never 0 (the C would trap):
//! 1 gives 0x8000, which wraps to -32768 in the i16, and 128..255 exercise the
//! unsigned byte read. unk2 covers 0, -1, 32767 and -32768 (the negation wraps).
//! RotMatrix_gte is a recording stub that writes some matrix fields with
//! ctx.value; the C ignores its result, so the stub returns null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0FA8));

pub const classes = [_][:0]const u8{ "separate", "aliased" };

pub const stubs = struct {
    pub fn RotMatrix_gte(ctx: *zd.Ctx, r: anytype, m: anytype) ?*v.MATRIX {
        _ = r;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        mat.m[0][0] = ctx.value(i16);
        mat.m[1][2] = ctx.value(i16);
        if (ctx.value(u1) == 1) mat.t[0] = ctx.value(i32);
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    var arg0: *v.func_800C1564_t = undefined;
    var arg1: *v.func_800C0FA8_t2 = undefined;
    if (ctx.chance(3)) {
        const block = ctx.allocBytes(64, 16);
        arg0 = @ptrCast(@alignCast(block));
        arg1 = @ptrCast(@alignCast(block));
    } else {
        arg0 = ctx.alloc(v.func_800C1564_t);
        arg1 = ctx.alloc(v.func_800C0FA8_t2);
    }
    const matrix = ctx.alloc(v.MATRIX);

    const edges = [_]i16{ 0, 1, -1, 32767, -32768 };
    arg0.unk2 = ctx.pick(i16, &edges);
    arg0.unk4.values[3] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    for (0..3) |i| {
        const d = ctx.pick(u8, &.{ 1, 2, 3, 127, 128, 255 });
        arg0.unk4.values[i] = if (d == 0) 1 else d;
    }
    arg0.unk8.vx = ctx.pick(i16, &edges);
    arg0.unk8.vy = ctx.pick(i16, &edges);
    arg0.unk8.vz = ctx.pick(i16, &edges);

    return .{ arg0, arg1, matrix };
}

pub fn classify(args: Args) usize {
    return if (@intFromPtr(args[0].?) == @intFromPtr(args[1].?)) 1 else 0;
}
