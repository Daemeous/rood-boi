//! zig_diff spec: func_800846F0 -> func_800846F0.func_800846F0 (146C.c).
//! Inputs: D_800F19CC is an arena block (the current action's unk2 is 4 or not, at the test's
//! edges); the stat change (the stub's result) and the target's hp take
//! edge values so the 16-bit sum wraps; action and source are null or
//! arena blocks (the C only passes them on).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800846F0));

pub const classes = [_][:0]const u8{ "match", "other" };

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
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    block.unk8.unk2 = ctx.edge(u8, &.{ 3, 4, 4, 5, 0xFF, 0x80 });
    const e = entities(ctx);
    return .{ e[0], e[1], e[2], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    const block = v.g.D_800F19CC().*.?;
    return if (block.unk8.unk2 == 4) 0 else 1;
}
