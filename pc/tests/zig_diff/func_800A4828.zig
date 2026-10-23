//! zig_diff spec: func_800A4828 -> func_800A4828.func_800A4828 (3A1A0.c).
//! Inputs: arg0 in 0..16 (every entry of the D_800F4538 table is a distinct
//! arena object, so the table lookup is observable), each object's visible bit
//! set or clear, and arg1 either a random arena MATRIX or null. The three
//! callees are recording stubs that write what the real ones can change: the
//! matrix's translation, and the visible bit of the object they are passed,
//! which the C's restore must overwrite.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4828));

pub const classes = [_][:0]const u8{ "hidden", "visible", "null-matrix" };

pub const stubs = struct {
    pub fn func_800AECA0(ctx: *zd.Ctx, m: anytype) void {
        if (m) |q| {
            const mx: *v.MATRIX = @ptrCast(@alignCast(q));
            mx.t[0] = ctx.value(i32);
        }
    }

    pub fn func_800AEAE8(ctx: *zd.Ctx, obj: anytype) void {
        if (obj) |q| {
            const t: *v.D_800F4538_t = @ptrCast(@alignCast(q));
            psx.setBits(&t.unk0, "visible", ctx.value(u1));
        }
    }

    pub fn func_800B28A8(ctx: *zd.Ctx, obj: anytype, m: anytype, k: c_int) void {
        _ = k;
        _ = m;
        if (obj) |q| {
            const t: *v.D_800F4538_t = @ptrCast(@alignCast(q));
            if (ctx.chance(2)) psx.setBits(&t.unk0, "visible", ctx.value(u1));
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    for (tbl) |*e| e.* = ctx.alloc(v.D_800F4538_t);
    const arg0 = ctx.range(c_int, 0, 16);
    const m: ?*v.MATRIX = if (ctx.chance(8)) null else ctx.alloc(v.MATRIX);
    psx.setBits(&tbl[@intCast(arg0)].?.unk0, "visible", if (ctx.chance(2)) 1 else 0);
    return .{ arg0, m };
}

pub fn classify(args: Args) usize {
    if (args[1] == null) return 2;
    const t = v.g.D_800F4538()[@intCast(args[0])].?;
    return if (psx.getBits(&t.unk0, "visible") != 0) 1 else 0;
}
