//! zig_diff spec: func_800AA698 -> func_800AA698.func_800AA698 (40564.c).
//! Inputs: an index into D_800F4538 (0..16, the table entry set to an arena
//! actor or null), arg1 an arena SVECTOR with vectors drawn from zero half the
//! time and from edges (+-1, 127, -128, the i16 extremes) otherwise, so squares
//! and position sums wrap; arg1 may be null when the actor is null (the C
//! returns before reading it). arg2 covers 0, -1, and other values including
//! 256 and 65536 (the u16 store wraps). unk5C2 covers 0 (dist 0, the goto
//! path), small values and 0xFFFF. The vs_gte_rsqrt stub returns 0, a small
//! value of either sign, or any int, so dist & 0x7F, dist / 128 and the
//! unk1A == 0 goto are all taken. It changes no memory the C reads (pure GTE
//! math). The three `temp < 0` true edges are dead: a square of an i16 lies in
//! [0, 2^30], so it is never negative, so they are waived.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA698));

pub const classes = [_][:0]const u8{ "no-actor", "arg2-zero", "arg2-minus-one", "arg2-other" };

pub const stubs = struct {
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const small: c_int = ctx.value(u8);
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => small,
            2 => -small,
            else => ctx.value(c_int),
        };
    }
};

fn vec(ctx: *zd.Ctx) i16 {
    if (ctx.chance(2)) return 0;
    return ctx.pick(i16, &.{ 1, -1, 127, -128, 32767, -32768 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx: c_int = if (ctx.chance(2)) ctx.edge(c_int, &.{ 0, 16 }) else ctx.range(c_int, 0, 16);
    const actor: ?*v.D_800F4538_t = if (ctx.chance(6)) null else ctx.alloc(v.D_800F4538_t);
    table[@intCast(idx)] = actor;
    if (actor) |a| {
        a.unk5C2 = ctx.pick(u16, &.{ 0, 1, 2, 127, 128, 255, 0xFFFF });
    }
    const vec_arg = ctx.alloc(v.SVECTOR);
    vec_arg.vx = vec(ctx);
    vec_arg.vy = vec(ctx);
    vec_arg.vz = vec(ctx);
    const arg1: ?*v.SVECTOR = if (actor == null and ctx.chance(2)) null else vec_arg;
    const arg2: c_int = ctx.pick(c_int, &.{ 0, -1, 1, -2, 255, 256, 65536, -256 });
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    if (table[@intCast(args[0])] == null) return 0;
    return switch (args[2]) {
        0 => 1,
        -1 => 2,
        else => 3,
    };
}
