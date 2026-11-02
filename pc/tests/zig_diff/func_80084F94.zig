//! zig_diff spec: func_80084F94 -> func_80084F94.func_80084F94 (146C.c).
//! Inputs: the stat change (the stub's result: zero a third of the time,
//! else edges) and the risk take edge values so the 16-bit sum wraps.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084F94));

pub const classes = [_][:0]const u8{ "zero", "nonzero" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";
var amount: i16 = 0;

pub const stubs = struct {
    pub fn _calculateStatChange(_: *zd.Ctx, a: anytype, b: anytype, t: anytype, h: anytype, e: anytype) i16 {
        _ = a;
        _ = b;
        _ = t;
        _ = h;
        _ = e;
        return amount;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    amount = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, 100, 0x7FFF, -0x8000, 0x100 });
    const e = ctx.alloc(v._hitEntity_t);
    e.risk = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    const action: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const source: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    return .{ action, source, e, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return if (amount == 0) 0 else 1;
}
