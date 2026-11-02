//! zig_diff spec: func_80076784 -> func_80076784.func_80076784 (146C.c).
//! Inputs: arena stats and init data (random bytes) with HP/MP at 0, -1,
//! 999, 1000, 9..11 and the i16 edges, MP 0 sometimes, the stats at the
//! byte edges; varied or not (vs_main_getRandSmoothed's stub 0..20); unk28
//! of both signs; the part stub a part or -1.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80076784));

pub const classes = [_][:0]const u8{ "fixed", "varied" };

pub const stubs = struct {
    pub fn vs_main_getRandSmoothed(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.edge(c_int, &.{ 0, 10, 20, 9, 11 });
    }
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        return ctx.edge(c_int, &.{ -1, 0, 3, 0x1FF });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const init = ctx.alloc(v.vs_battle_charInitData);
    init.hp = ctx.pick(i16, &.{ 0, -1, 9, 10, 11, 999, 1000, 1009, 0x7FFF, -0x8000 });
    init.mp = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ -1, 9, 999, 1000, 0x7FFF });
    init.strength = ctx.pick(u8, &.{ 0, 5, 255 });
    init.unk28 = ctx.pick(i32, &.{ 0, 1, -1 });
    return .{ ctx.int(c_int), ctx.alloc(v.vs_battle_actor2), init, if (ctx.chance(2)) 0 else 1 };
}

pub fn classify(args: Args) usize {
    return if (args[3] != 0) 1 else 0;
}
