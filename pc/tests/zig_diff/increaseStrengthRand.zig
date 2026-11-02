//! zig_diff spec: _increaseStrengthRand -> increase_strength_rand.increaseStrengthRand (146C.c).
//! Inputs: an arena target entity whose strength takes edge values (0, the
//! i16 limits, near them), and vs_main_getRand's result: 0..3 (what it
//! returns in the game) or any value (the C does not bound it), so the sum
//! wraps at 16 bits both ways. The other arguments are unused.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").increaseStrengthRand));

pub const classes = [_][:0]const u8{ "in-range", "wraps" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

var rand: c_int = 0;

pub const stubs = struct {
    pub fn vs_main_getRand(_: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return rand;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    rand = if (ctx.chance(2)) ctx.range(c_int, 0, 3) else ctx.pick(c_int, &.{ -1, -2, 0x7FFF, 0x8000, 0xFFFF, 0x7FFF_FFFF });
    const entity = ctx.alloc(v._hitEntity_t);
    entity.strength = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, 0x7FFE, 0x7FFC, -0x8000, 100 });
    const a0: ?*v.vs_action_t = if (ctx.chance(4)) null else ctx.alloc(v.vs_action_t);
    const a1: ?*v._hitEntity_t = if (ctx.chance(4)) null else ctx.alloc(v._hitEntity_t);
    return .{ a0, a1, entity, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const sum = @as(i64, args[2].?.strength) + rand + 1;
    return if (sum < -0x8000 or sum > 0x7FFF) 1 else 0;
}
