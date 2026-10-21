//! zig_diff spec: func_800A249C -> turn_toward.turnToward (38C1C.c).
//! Inputs: a D_800F4538 slot (0-7) holding a random actor whose unk0 has
//! random bytes, with unkA_5 (stop), unkA_6 (weapon drawn), unkC_0 and the
//! animation id set explicitly. The turn (arg1 - facing) is biased onto
//! 0, +-0x800 (the exact bound), +-0x1000 (a wrap that lands on 0 or on the
//! other side), small turns of either sign, and random i16-range targets.
//! The `var_a1 < 0` test sees both signs; it can never see equal sides
//! because a zero turn returns before it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").turnToward));

pub const classes = [_][:0]const u8{
    "stopped",
    "on-target",
    "anim-76",
    "anim-75",
    "unkC-set",
    "bound-edge",
    "turn-wrap",
    "short-unkA6",
    "short-left",
    "short-right",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const slots = psx.globalMany(*v.D_800F4538_t, "D_800F4538");
    const idx = ctx.range(u8, 0, 7);
    slots[idx] = actor;

    psx.setBits(&actor.unk0, "unkA_5", @intFromBool(ctx.chance(8)));
    psx.setBits(&actor.unk0, "unkA_6", @intFromBool(ctx.chance(2)));
    psx.setBits(&actor.unk0, "unkC_0", if (ctx.chance(5)) ctx.int(u4) else 0);
    actor.animationId = ctx.pick(u8, &.{ 0x75, 0x76, 0x74, 0x77, 0 });
    actor.unk0.facing = ctx.pick(i16, &.{ 0, 0x800, -0x800, 0x7FFF, -0x8000, 0x1000, -1 });

    const f: c_int = actor.unk0.facing;
    const arg1: c_int = switch (ctx.random.uintLessThan(u8, 8)) {
        0 => f,
        1 => f + 0x800,
        2 => f - 0x800,
        3 => f + 0x1000,
        4 => f - 0x1000,
        5, 6 => f + ctx.range(c_int, -0x7FF, 0x7FF),
        else => @as(c_int, ctx.range(i16, -0x8000, 0x7FFF)),
    };
    return .{ @as(c_int, idx), arg1 };
}

pub fn classify(args: Args) usize {
    const slots = psx.globalMany(*v.D_800F4538_t, "D_800F4538");
    const actor = slots[@intCast(args[0])];
    const u = &actor.unk0;
    if (psx.getBits(u, "unkA_5") != 0) return 0;
    const d = args[1] - @as(c_int, u.facing);
    if (d == 0) return 1;
    if (actor.animationId == 0x76) return 2;
    if (actor.animationId == 0x75) return 3;
    if (psx.getBits(u, "unkC_0") != 0) return 4;
    if (d == 0x800 or d == -0x800) return 5;
    if (d > 0x800 or d < -0x800) return 6;
    if (psx.getBits(u, "unkA_6") != 0) return 7;
    return if (d < 0) 8 else 9;
}
