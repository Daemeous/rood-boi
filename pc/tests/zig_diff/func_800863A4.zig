//! zig_diff spec: func_800863A4 -> func_800863A4.func_800863A4 (146C.c).
//! Inputs: an action 0..255 of each kind (its unk2_4 set by prepare: 0..15);
//! user and target actors 0..3 (arena actors with arena stats); a limb;
//! arena from/to points at the 16-bit edges. func_800A1B9C's stub writes a
//! limb point or reports none, func_800C1564's stub records the area and
//! point and answers in or out; func_80085B10's stub records the whole
//! request (a stack local the trace only tags); ratan2, vs_gte_rsqrt and
//! _getActionCost return stub values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800863A4));

pub const classes = [_][:0]const u8{ "none", "body", "limb", "area", "aimed" };

pub const stubs = struct {
    pub fn func_800A1B9C(ctx: *zd.Ctx, actor: anytype, limb: anytype, out: anytype, mode: anytype) c_int {
        _ = actor;
        _ = limb;
        _ = mode;
        out[0] = ctx.value(u16);
        out[1] = ctx.value(u16);
        out[2] = ctx.value(u16); // the third halfword lands past the C's u_short[2]
        return ctx.edge(c_int, &.{ -1, 0, 1 });
    }
    pub fn func_800C1564(ctx: *zd.Ctx, area: anytype, point: anytype) c_int {
        const a: *const v.func_800C1564_t = @ptrCast(@alignCast(area.?));
        const seen = ctx.alloc(v.func_800C1564_t);
        seen.* = a.*;
        const p = ctx.alloc([3]u16);
        p[0] = point[0];
        p[1] = point[1];
        p[2] = point[2];
        return if (ctx.value(u1) == 0) 0 else 1;
    }
    pub fn func_80085B10(ctx: *zd.Ctx, action: anytype, result: anytype, request: anytype, flag: anytype) void {
        _ = action;
        _ = result;
        _ = flag;
        const seen = ctx.alloc(v.D_800F19CC_t2);
        seen.* = @as(*const v.D_800F19CC_t2, @ptrCast(@alignCast(request.?))).*;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    for (0..4) |k| {
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.stats.raw = psx.addr(ctx.alloc(v.vs_battle_actor2));
        v.g.vs_battle_actors()[k] = actor;
    }
    const action = ctx.range(c_int, 0, 255);
    psx.setBits(&v.g.actions()[@intCast(action)], "unk2_4", ctx.range(u4, 0, 15));
    const from = ctx.alloc(v.SVECTOR);
    const to = ctx.alloc(v.SVECTOR);
    from.vx = ctx.pick(i16, &.{ 0, 0x7FFF, -0x8000 });
    to.vz = ctx.pick(i16, &.{ 0, 0x7FFF, -0x8000 });
    return .{ action, ctx.range(c_int, 0, 3), ctx.range(c_int, 0, 3), ctx.pick(c_int, &.{ 0, 5, -1, 0x80 }), from, to, ctx.alloc(v.D_800F19CC_t2) };
}

pub fn classify(args: Args) usize {
    return switch (psx.getBits(&v.g.actions()[@intCast(args[0])], "unk2_4")) {
        1, 3 => 1,
        5, 7 => 2,
        2, 4, 6, 8 => 3,
        11, 12 => 4,
        else => 0,
    };
}
