//! zig_diff spec: func_800C1034 -> func_800C1034.func_800C1034 (58578.c).
//! Inputs: a random func_800C1564_t and a three-entry u_short buffer. The
//! callees are stubs. func_800C0FA8 fills the work block and the matrix;
//! ApplyMatrixSV writes the output vector and the unk18 terms in one of four
//! modes chosen per iteration: echo of the first loop's differences (the
//! sink records them), small values, threshold values whose sum of squares
//! lands on the `(sum >> 16) < 1u` edge, and wide values whose sum wraps
//! negative (the unsigned compare).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
const c = @import("c_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1034));

pub const classes = [_][:0]const u8{ "echo", "small", "threshold", "wide" };

/// Which ApplyMatrixSV behaviour this iteration takes (set by prepare).
var mode: u8 = 0;
/// Arena block where ApplyMatrixSV records the vector it was given.
var sink: *[3]i16 = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.func_800C1564_t);
    const buf: [*c]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(6, 2)));
    for (0..3) |k| buf[k] = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000, 0x7FFF, 0x00FF });
    sink = ctx.alloc([3]i16);
    mode = ctx.range(u8, 0, 3);
    return .{ arg0, buf };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}

fn edge(ctx: *zd.Ctx) i16 {
    const e = [_]i16{ 0, 1, -1, 2, -2, 128, -128, 181, -181, 182, 256, -256 };
    return e[ctx.value(u8) % e.len];
}

pub const stubs = struct {
    pub fn func_800C0FA8(ctx: *zd.Ctx, arg0: anytype, work: anytype, m: anytype) void {
        _ = arg0;
        const w: *v.func_800C0FA8_t2 = @ptrCast(@alignCast(work));
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (&w.unk10) |*e| e.* = ctx.value(i16);
        for (&w.unk18) |*e| e.* = if ((ctx.value(u8) & 1) == 0) 4096 else ctx.value(i16);
        for (&mat.m) |*row| {
            for (row) |*e| e.* = ctx.value(i16);
        }
        for (&mat.t) |*e| e.* = ctx.value(i32);
    }

    pub fn ApplyMatrixSV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) [*c]c.SVECTOR {
        _ = m;
        const src: *v.SVECTOR = @ptrCast(@alignCast(v0));
        const dst: *v.SVECTOR = @ptrCast(@alignCast(v1));
        // unk18 sits 16 bytes after unk8 (the output vector): the C reads it next.
        const t: *[4]i16 = @ptrCast(@alignCast(@as([*]u8, @ptrCast(dst)) + 16));
        sink.* = .{ src.vx, src.vy, src.vz };
        if (mode == 0) {
            dst.* = src.*;
        } else if (mode == 1) {
            dst.vx = src.vx >> 9;
            dst.vy = src.vy >> 9;
            dst.vz = src.vz >> 9;
        } else if (mode == 2) {
            dst.vx = edge(ctx);
            dst.vy = edge(ctx);
            dst.vz = edge(ctx);
            t.* = .{ 4096, 4096, 4096, 4096 };
        } else {
            dst.vx = ctx.value(i16);
            dst.vy = ctx.value(i16);
            dst.vz = ctx.value(i16);
            for (t) |*e| e.* = ctx.value(i16);
        }
        return null;
    }
};
