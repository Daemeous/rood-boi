//! zig_diff spec: func_800C110C -> func_800C110C.func_800C110C (58578.c).
//! Inputs: a func_800C1564_t in the arena whose flag byte (unk4 byte 1) is an
//! edge or random value, a three-entry u16 array of edge and random values,
//! and arg2 zero, positive or negative (sign-bit and small edges). The callee
//! func_800C0FA8 is a stub that writes work.unk10 and work.unk18 in full and
//! sometimes rewrites the flag byte. ApplyMatrixSV writes work.unk8 with small
//! components (so the term sums land in [0, 131071] and `sum >> 16` sees 0 and
//! 1) or full-range ones (so products and sums wrap). A quarter of its calls
//! make `(u_short)-vy` equal to `flag << 5` exactly, so the clamp test sees
//! equal sides and the inner block runs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
const c = @import("c_58578");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C110C));

pub const classes = [_][:0]const u8{ "arg2-zero", "arg2-positive", "arg2-negative" };

// Set by prepare, read by the stubs (the same in both runs).
var cur: ?*v.func_800C1564_t = null;
var cur_arg2: c_int = 0;

/// A component mostly within (-2000, 2000), sometimes the full i16 range.
fn smallI16(ctx: *zd.Ctx) i16 {
    if (ctx.value(u8) < 128) return @rem(ctx.value(i16), 2000);
    return ctx.value(i16);
}

pub const stubs = struct {
    pub fn func_800C0FA8(ctx: *zd.Ctx, a0: anytype, w: anytype, m: anytype) void {
        _ = a0;
        const actor = cur.?;
        const work: *v.func_800C0FA8_t2 = @ptrCast(@alignCast(w));
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (0..4) |k| {
            work.unk10[k] = smallI16(ctx);
            work.unk18[k] = smallI16(ctx);
        }
        for (&mat.m) |*row| for (row) |*e| {
            e.* = ctx.value(i16);
        };
        mat.t = .{ ctx.value(i32), ctx.value(i32), ctx.value(i32) };
        // The callee may change the flag byte the C reads back after the call.
        if (ctx.value(u8) < 64) actor.unk4.values[1] = ctx.value(u8);
    }

    pub fn ApplyMatrixSV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) [*c]c.SVECTOR {
        _ = m;
        _ = v0;
        const out: *v.SVECTOR = @ptrCast(@alignCast(v1));
        const mode = ctx.value(u8);
        out.vx = smallI16(ctx);
        out.vy = smallI16(ctx);
        out.vz = smallI16(ctx);
        if (mode < 80) {
            // Make (u_short)-vy after the arg2 step equal to flag << 5.
            const flag: i32 = cur.?.unk4.values[1];
            const t: i32 = flag << 5;
            const x: i32 = if (cur_arg2 != 0) flag << 4 else 0;
            out.vy = @truncate(x - t);
        }
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.func_800C1564_t);
    actor.unk4.values[1] = ctx.pick(u8, &.{ 0, 1, 2, 3, 0x7F, 0x80, 0xFF });

    const buf: [*c]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(6, 2)));
    for (0..3) |k| buf[k] = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });

    const arg2 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });

    cur = actor;
    cur_arg2 = arg2;
    return .{ actor, buf, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] == 0) return 0;
    if (args[2] < 0) return 2;
    return 1;
}
