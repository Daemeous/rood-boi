//! zig_diff spec: func_8009DF3C -> func_8009DF3C.func_8009DF3C (30DB0.c).
//! Inputs: an actor built in the arena and placed in the D_800F4538 table at a
//! random index 0..16. unkF is zero in about one run in five, otherwise 1, 0x80
//! or 0xFF (the sign-bit case for the unkF test). arg1 is picked from 0-3, 4, -1
//! (the switch default and negative selectors), with 3 weighted so `arg1 < 3`
//! sees equal sides often. animationId covers 0xC6, 0xC8 and the neighbours, so
//! case 3 takes both the skip return and the continue path. func_800AD494 is a
//! stub that writes sp10[0] as null, a sign-bit pointer, or an arena block, so
//! the `sp10[0] == 0` branch goes both ways. The stub's return value is drawn
//! but the C ignores it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DF3C));

pub const classes = [_][:0]const u8{
    "early-return",
    "case0",
    "case1",
    "case2",
    "case3-skip",
    "case3-anim",
    "default",
};

pub const stubs = struct {
    pub fn func_800AD494(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype) c_int {
        _ = a;
        _ = b;
        const T = @TypeOf(out[0]);
        const p: T = switch (ctx.value(u2)) {
            0 => null,
            1 => @ptrFromInt(zd.low_ram),
            else => @ptrCast(@alignCast(ctx.allocBytes(16, 16))),
        };
        out[0] = p;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const idx = ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(idx)] = actor;
    if (ctx.chance(5)) {
        actor.unk0.unkF = 0;
    } else {
        actor.unk0.unkF = ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    }
    actor.animationId = ctx.pick(u8, &.{ 0xC6, 0xC8, 0xC7, 0xC9, 0 });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, 2, 3, 3, 4, -1 });
    return .{ idx, arg1 };
}

pub fn classify(args: Args) usize {
    const t = v.g.D_800F4538()[@intCast(args[0])].?;
    if (t.unk0.unkF == 0) return 0;
    return switch (args[1]) {
        0 => 1,
        1 => 2,
        2 => 3,
        3 => if (t.animationId == 0xC6 or t.animationId == 0xC8) 5 else 4,
        else => 6,
    };
}
