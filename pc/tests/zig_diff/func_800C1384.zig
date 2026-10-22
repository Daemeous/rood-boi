//! zig_diff spec: func_800C1384 -> func_800C1384.func_800C1384 (58578.c).
//! Inputs: a shape in the arena (its origin and unk2 at the i16 edges, its
//! values[1] bound at 0, 1, 255, 128 or random), a position of four u16 with
//! random values (the differences wrap through the short store), and angled
//! at 0, 1, -1, INT_MIN, 256, 2. The shape's values[0] and values[2] are both
//! zero in about a third of the runs: the four corners then coincide at the
//! origin, the NCLIP result is 0, and the `clip < 0` comparison sees equal
//! sides. The ApplyMatrixSV stub reads its v0 input (the local vector the
//! loop writes), so the position-minus-origin stores reach the rotated
//! output; it picks the rotated vy relative to the bound (equal, just above,
//! just below, or random), so the Y test takes both branches. RotMatrix_gte
//! and ApplyMatrixSV write their outputs with ctx.value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1384));

/// The shape the stub reads, and the Y mode (0 equal, 1 above, 2 below, 3 random).
var g_shape: ?*v.func_800C1564_t = null;
var g_mode: u8 = 0;

pub const classes = [_][:0]const u8{
    "above-bound",
    "above-bound-zero-xz",
    "equal-bound",
    "equal-bound-zero-xz",
    "below-bound",
    "below-bound-zero-xz",
    "random-y",
    "random-y-zero-xz",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const shape = ctx.alloc(v.func_800C1564_t);
    const pos = ctx.alloc([4]u16);
    g_shape = shape;

    shape.unk2 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    shape.unk8.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    shape.unk8.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    shape.unk8.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    const degenerate = ctx.chance(3);
    shape.unk4.values[0] = if (degenerate) 0 else ctx.int(u8);
    shape.unk4.values[2] = if (degenerate) 0 else ctx.int(u8);
    shape.unk4.values[1] = ctx.pick(u8, &.{ 0, 1, 255, 128 });
    shape.unk4.values[3] = ctx.int(u8);

    const t: u16 = @as(u16, shape.unk4.values[1]) << 5;
    g_mode = ctx.range(u8, 0, 3);
    if (g_mode == 2 and t == 0) g_mode = 0;

    for (pos) |*p| p.* = ctx.int(u16);
    const angled = ctx.pick(c_int, &.{ 0, 1, -1, std.math.minInt(c_int), 256, 2 });
    return .{ shape, @ptrCast(pos), angled };
}

pub fn classify(args: Args) usize {
    const shape = args[0].?;
    const zero = shape.unk4.values[0] == 0 and shape.unk4.values[2] == 0;
    const m: usize = switch (g_mode) {
        1 => 0,
        0 => 1,
        2 => 2,
        else => 3,
    };
    return m * 2 + @intFromBool(zero);
}

pub const stubs = struct {
    /// The GTE is a recording stub: MFC2 answers 0 a quarter of the time (the
    /// NCLIP result read by gte_stopz then sits on the `clip < 0` edge), else any value.
    pub fn MFC2(ctx: *zd.Ctx, reg: anytype) c_uint {
        _ = reg;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_uint);
    }

    pub fn RotMatrix_gte(ctx: *zd.Ctx, r: anytype, m: anytype) @TypeOf(m) {
        _ = r;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (&mat.m) |*row| {
            for (row) |*e| e.* = ctx.value(i16);
        }
        mat.t = .{ ctx.value(i32), ctx.value(i32), ctx.value(i32) };
        return m;
    }

    pub fn ApplyMatrixSV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) @TypeOf(v1) {
        _ = m;
        const src: *const v.SVECTOR = @ptrCast(@alignCast(v0));
        const out: *v.SVECTOR = @ptrCast(@alignCast(v1));
        const shape = g_shape.?;
        const zero = shape.unk4.values[0] == 0 and shape.unk4.values[2] == 0;
        // In the degenerate case the corners all sit at the origin, so the
        // rotated vx and vz stay 0 and the NCLIP of the four corners is 0.
        out.vx = if (zero) 0 else src.vx +% ctx.value(i16);
        out.vz = if (zero) 0 else src.vz +% ctx.value(i16);
        const t: u16 = @as(u16, shape.unk4.values[1]) << 5;
        out.vy = switch (g_mode) {
            0 => @bitCast(@as(u16, 0) -% t),
            1 => @bitCast(@as(u16, 0) -% (t +% 1)),
            2 => @bitCast(@as(u16, 0) -% (t -% 1)),
            else => ctx.value(i16),
        };
        return v1;
    }
};
