//! zig_diff spec: func_800DA6A4 -> func_800DA6A4.func_800DA6A4 (71618.c).
//! Inputs: a da4bcState in the arena whose flagE is zero, nonzero without the
//! sign bit, or with it set. The callees' results are drawn to hit every test:
//! the target and second checks see negative, zero and positive values, and
//! func_800DB370 returns zero or not. The stubs also write state fields the C
//! never reads back, so the state bytes must match as well.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DA6A4));

/// The state prepare built, for stubs that write into it.
var cur: ?*v.da4bcState = null;

pub const classes = [_][:0]const u8{ "flagE-zero", "flagE-low", "flagE-sign" };

pub const stubs = struct {
    pub fn func_800DB61C(ctx: *zd.Ctx, state: anytype, n: anytype) c_int {
        _ = state;
        _ = n;
        if (ctx.value(u1) == 1) cur.?.mode = ctx.value(u8);
        return ctx.value(c_int);
    }

    pub fn func_800DBB2C(ctx: *zd.Ctx, state: anytype, a: anytype, b: anytype) c_int {
        _ = state;
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) cur.?.target15C = ctx.value(u8);
        const r = ctx.value(u8);
        if (r < 60) return 0;
        if (r < 120) return -1;
        return ctx.value(c_int);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, state: anytype, target: anytype, a: anytype, b: anytype) c_int {
        _ = state;
        _ = target;
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) cur.?.flag18 = ctx.value(u8);
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800DA1D4(ctx: *zd.Ctx, state: anytype) c_int {
        _ = state;
        cur.?.action = ctx.value(u8);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.da4bcState);
    st.flagE = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF, 2 });
    cur = st;
    return .{st};
}

pub fn classify(args: Args) usize {
    const f = args[0].?.flagE;
    if (f == 0) return 0;
    return if (f & 0x80 != 0) 2 else 1;
}
