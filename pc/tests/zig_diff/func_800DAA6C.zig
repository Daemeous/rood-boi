//! zig_diff spec: func_800DAA6C -> func_800DAA6C.func_800DAA6C (71618.c).
//! Inputs: a da4bcState in the arena whose flagE is zero or nonzero (nonzero
//! with and without the sign bit). The two callees are recording stubs whose
//! return values are drawn by ctx.value: func_800DB61C returns zero or a
//! nonzero value with and without the sign bit, and func_800DA1D4 likewise.
//! The void callees write flag18, which the C never reads back, so the state
//! hash at each call still tracks what the stubs do.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DAA6C));

pub const classes = [_][:0]const u8{ "flagE", "no-flagE" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.da4bcState);
    state.flagE = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    return .{state};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.flagE != 0) 0 else 1;
}

pub const stubs = struct {
    pub fn func_800DB61C(ctx: *zd.Ctx, p: anytype, n: anytype) c_int {
        _ = n;
        const s: *v.da4bcState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.flag18 = ctx.value(u8);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DA1D4(ctx: *zd.Ctx, p: anytype) c_int {
        const s: *v.da4bcState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.flag18 = ctx.value(u8);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DB5B0(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.da4bcState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.flag18 = ctx.value(u8);
    }

    pub fn func_800DB5FC(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.da4bcState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.flag18 = ctx.value(u8);
    }
};
