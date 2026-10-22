//! zig_diff spec: func_800C0D78 -> func_800C0D78.func_800C0D78 (58578.c).
//! Inputs: D_800EB9B8 null (the early return) or an arena D_800EB9B8_t whose
//! two shapes take every type 0-255 (the switch's cases 1-7 and default, with
//! the 0/8/255 defaults), angles from 0, +-1, 32767 and -32768, and random
//! shape bytes, so the u8 shifts and adds wrap and the i16 matrix terms run
//! negative. The stubs write the shape vector the C reads back after
//! func_800C02A8, a scratchpad field, and a next-shape type through
//! D_800EB9B8 (read on the second iteration).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0D78));

pub const classes = [_][:0]const u8{ "null-global", "case-2-or-4", "case-1-3-5-7", "default-only" };

/// The matrix func_800C085C returns: allocated once per iteration by prepare.
var out_matrix: *v.MATRIX = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800EB9B8_t);
    out_matrix = ctx.alloc(v.MATRIX);
    if (ctx.chance(8)) {
        v.g.D_800EB9B8().* = null;
    } else {
        v.g.D_800EB9B8().* = d;
        for (0..2) |i| {
            d.unk0[i].unk0 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 255 });
            d.unk0[i].unk2 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        }
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const dp = v.g.D_800EB9B8().* orelse return 0;
    var has24 = false;
    var hasOther = false;
    for (dp.unk0) |s| {
        switch (s.unk0) {
            2, 4 => has24 = true,
            1, 3, 5, 6, 7 => hasOther = true,
            else => {},
        }
    }
    if (has24) return 1;
    if (hasOther) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }

    pub fn func_800C02A8(ctx: *zd.Ctx) void {
        // The shape's vector is rewritten: the C loads it into GTE after this call.
        if (ctx.value(u1) == 1) {
            const s: *v.func_800C1564_t = @ptrFromInt(0x1F800378);
            s.unk8.vx = ctx.value(i16);
        }
    }

    pub fn func_800C085C(ctx: *zd.Ctx, scale: anytype, angle: anytype) [*c]v.MATRIX {
        _ = angle;
        if (ctx.value(u2) == 0) scale[0] = ctx.value(u8);
        out_matrix.m[0][1] = ctx.value(i16);
        out_matrix.m[1][1] = ctx.value(i16);
        out_matrix.m[2][1] = ctx.value(i16);
        return out_matrix;
    }

    pub fn func_800C253C(ctx: *zd.Ctx, t: anytype) void {
        _ = t;
        if (ctx.value(u1) == 1) {
            const s: *v.func_800C1564_t = @ptrFromInt(0x1F800378);
            s.unk2 = ctx.value(i16);
        }
    }

    pub fn func_800C0B50(ctx: *zd.Ctx, shape: anytype, color: anytype) void {
        _ = shape;
        _ = color;
        // The next shape is copied from D_800EB9B8->unk0[1] on the second pass.
        if (v.g.D_800EB9B8().*) |d| {
            if (ctx.value(u2) == 0) d.unk0[1].unk0 = ctx.value(u8);
        }
    }
};
