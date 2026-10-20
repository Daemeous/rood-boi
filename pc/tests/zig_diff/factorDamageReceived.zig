//! zig_diff spec: _factorDamageReceived -> hit_factors.factorDamageReceived (146C.c).
//! Inputs: D_800F19CC an arena action block whose received hit has any
//! hpEffect (0-3), hp and mp at the i16 edges (so hp + mp leaves i16 and the
//! product is negative), an arena action whose two hits have any statFactor
//! (0-31), and hit number 0 or 1. The other three arguments are unused.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").factorDamageReceived));

pub const classes = [_][:0]const u8{ "no-effect", "effect-1", "effect-2", "effect-3", "negative", "i16-overflow" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    const hit = &block.unk8.unk4;
    psx.setBits(hit, "hpEffect", ctx.range(u2, 0, 3));
    const edges = [_]i16{ 0, 1, -1, 9, -9, 10, 32767, -32768, 16384 };
    hit.unk0.hp = ctx.pick(i16, &edges);
    hit.unk0.mp = ctx.pick(i16, &edges);
    const action = ctx.alloc(v.vs_action_t);
    if (ctx.chance(4)) psx.setBits(&action.hitParams[ctx.range(usize, 0, 1)], "statFactor", ctx.pick(u5, &.{ 0, 1, 31 }));
    return .{ action, ctx.alloc(v._hitEntity_t), ctx.alloc(v._hitEntity_t), ctx.range(c_int, 0, 1), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const hit = &v.g.D_800F19CC().*.?.unk8.unk4;
    const effect = psx.getBits(hit, "hpEffect");
    if (effect != 1 and effect != 3) return if (effect == 0) 0 else 2;
    const sum = @as(c_int, hit.unk0.hp) + hit.unk0.mp;
    if (sum > 32767 or sum < -32768) return 5;
    const factor: c_int = psx.getBits(&args[0].?.hitParams[@intCast(args[3])], "statFactor");
    if (sum * factor < 0) return 4;
    return if (effect == 1) 1 else 3;
}
