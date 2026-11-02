//! zig_diff spec: func_80084E28 -> func_80084E28.func_80084E28 (146C.c).
//! Inputs: the stat change (the stub's result) and the target's hp take
//! edge values so the 16-bit sum wraps; the target's unk40 is 0, 1, -1 or
//! any value; action and source are null or arena blocks.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084E28));

pub const classes = [_][:0]const u8{ "recorded", "not-recorded" };

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

fn entities(ctx: *zd.Ctx) struct { ?*v.vs_action_t, ?*v._hitEntity_t, *v._hitEntity_t } {
    amount = ctx.pick(i16, &.{ 0, 1, -1, 100, 0x7FFF, -0x8000 });
    const target = ctx.alloc(v._hitEntity_t);
    target.unk0.hp = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000, 500 });
    const action: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const source: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    return .{ action, source, target };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const e = entities(ctx);
    e[2].unk40 = if (ctx.chance(2)) 0 else ctx.pick(i32, &.{ 1, -1, 0x100 });
    return .{ e[0], e[1], e[2], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    return if (args[2].?.unk40 == 0) 0 else 1;
}
