//! zig_diff spec: func_800BB68C -> func_800BB68C.func_800BB68C (4A0A8.c).
//! Inputs: a reference with bit 0x2000 (0x2003 the hostile filter, 0x2000,
//! 0x2001, 0xFFFF) or without (0, 0x1FFF, any), and an arena set (random
//! bytes). The func_8007CF64 stub returns null or one of four arena actors
//! whose allegiance is 4, 1, 0x8004 or random: the set fills with 0..17
//! entries.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB68C));

pub const classes = [_][:0]const u8{ "all", "hostile", "one" };

var actors: [4]*v.vs_battle_actor = undefined;
var density: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    for (&actors, [_]u16{ 4, 1, 0x8004, 0 }) |*a, al| {
        a.* = ctx.alloc(v.vs_battle_actor);
        a.*.allegiance = if (al == 0) ctx.int(u16) else al;
    }
    density = ctx.edge(u8, &.{ 0, 1, 4, 8 });
    const ref: u16 = if (ctx.chance(2))
        ctx.edge(u16, &.{ 0x2003, 0x2003, 0x2000, 0x2001, 0xFFFF, 0x2004 })
    else
        ctx.pick(u16, &.{ 0, 0x1FFF, 3, 0xDFFF }) & ~@as(u16, 0x2000);
    return .{ ref, ctx.alloc(v.func_800BB68C_t) };
}

pub fn classify(args: Args) usize {
    if (args[0] & 0x2000 == 0) return 2;
    return if (args[0] == 0x2003) 1 else 0;
}

pub const stubs = struct {
    pub fn func_8007CF64(ctx: *zd.Ctx, id: c_int) ?*v.vs_battle_actor {
        _ = id;
        const r = ctx.value(u8);
        if (density == 0 or r % 8 >= density) return null;
        return actors[(r >> 4) % 4];
    }
};
