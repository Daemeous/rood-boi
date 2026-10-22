//! zig_diff spec: func_800E153C -> func_800E153C.func_800E153C (771A8.c).
//! Inputs: a movementDestinationState in the arena with random position,
//! destination, previous, x/z, flags, flagD, attempts and distance. The
//! distance is kept within +-46340 so the C's distance*distance never
//! overflows. Edges: the masked position equal to the destination (early
//! fallback), flags&6 zero, set to 2 (the mask 4/6 case) or random, flagD
//! zero or not, previous's sign bit set, clear, or its <<8 exactly zero,
//! attempts at 0, 4, 5 and 0xFFFF (the ++ wrap). The stubs return the exact
//! squared distance (equal), one above and one below it, 16 and 15 from the
//! early-exit test, and 0, -1 or 1 from the path finder, so the loop and the
//! begin path are both taken.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E153C));

pub const classes = [_][:0]const u8{ "masked-equal", "flags-clear", "flagD-set", "attempts-short", "path-finder" };

var cur_state: ?*v.movementDestinationState = null;

fn asState(p: anytype) *v.movementDestinationState {
    if (@typeInfo(@TypeOf(p)) == .optional) return @ptrCast(@alignCast(p.?));
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    pub fn func_800E4660(ctx: *zd.Ctx, a: anytype, b: anytype, c2: anytype) c_uint {
        _ = a;
        _ = b;
        _ = c2;
        const d: u32 = @bitCast(cur_state.?.distance);
        const sq: u32 = d *% d;
        const r = ctx.value(u8);
        if (r < 80) return sq;
        if (r < 120) return sq +% 1;
        if (r < 160) return sq -% 1;
        return ctx.value(u32);
    }

    pub fn func_800E45F4(ctx: *zd.Ctx, a: anytype, b: anytype) c_uint {
        _ = a;
        _ = b;
        const r = ctx.value(u8);
        if (r < 64) return 16;
        if (r < 96) return 15;
        return @as(c_uint, ctx.value(u8) & 31);
    }

    pub fn func_800D954C(ctx: *zd.Ctx, p: anytype, dest: anytype) c_int {
        _ = dest;
        const s = asState(p);
        const r = ctx.value(u8);
        // the C reads state->destination again in its loop
        if (ctx.value(u8) < 64) s.destination.raw = ctx.value(u32);
        if (r < 120) return 0;
        if (r < 150) return -1;
        return 1;
    }

    // the real callee steers the actor: the C reads these back
    pub fn func_800E1388(ctx: *zd.Ctx, p: anytype, pos: anytype, angle: anytype, dist: anytype) void {
        _ = pos;
        _ = angle;
        _ = dist;
        const s = asState(p);
        if (ctx.value(u1) == 1) s.position.raw = ctx.value(u32);
        if (ctx.value(u2) == 0) s.flagD = ctx.value(u8);
        if (ctx.value(u2) == 0) s.flags = ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.movementDestinationState);
    cur_state = s;

    const dist_edges = [_]i32{ 0, 1, -1, 46340, -46340 };
    s.distance = if (ctx.chance(4))
        dist_edges[ctx.range(usize, 0, dist_edges.len - 1)]
    else
        ctx.range(i32, -46340, 46340);

    s.position.raw = ctx.int(u32);
    s.destination.raw = if (ctx.chance(4))
        (s.position.raw & 0xFF00FF) | (ctx.int(u32) & ~@as(u32, 0xFF00FF))
    else
        ctx.int(u32);
    s.previous.raw = ctx.pick(u32, &.{ 0, 0xFF00_0000, 0x0080_0000, 0x007F_FFFF, 0xFFFF_FFFF });
    s.x = ctx.int(i16);
    s.z = ctx.int(i16);
    s.flagD = if (ctx.chance(2)) 0 else ctx.int(u8);

    s.flags = ctx.int(u16);
    const fr = ctx.range(u8, 0, 3);
    if (fr == 0) s.flags &= ~@as(u16, 6);
    if (fr == 1) s.flags = (s.flags & ~@as(u16, 6)) | 2;

    s.attempts = ctx.pick(u16, &.{ 0, 4, 5, 0xFFFF });

    return .{ s, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if ((s.position.raw & 0xFF00FF) == (s.destination.raw & 0xFF00FF)) return 0;
    if (s.flags & 6 == 0) return 1;
    if (s.flagD != 0) return 2;
    if (s.attempts +% 1 < 6) return 3;
    return 4;
}
