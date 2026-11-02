//! zig_diff spec: func_800842AC -> func_800842AC.func_800842AC (146C.c).
//! Inputs: an arena target entity with edge hp values (the doubled value
//! wraps at 16 bits) and a target limb -8..9 (past the 6 limbs both ways,
//! inside the entity and the arena after it).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800842AC));

pub const classes = [_][:0]const u8{ "in-limbs", "outside" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx.allocBytes(64, 16);
    const e = ctx.alloc(v._hitEntity_t);
    e.unk0.hp = ctx.pick(i16, &.{ 0, 1, -1, 0x3FFF, 0x4000, -0x4000, -0x4001, 0x7FFF, -0x8000 });
    e.unk0.targetLimb = ctx.range(i8, -8, 9);
    _ = ctx.allocBytes(64, 16);
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), e, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const l = args[2].?.unk0.targetLimb;
    return if (l >= 0 and l < 6) 0 else 1;
}
