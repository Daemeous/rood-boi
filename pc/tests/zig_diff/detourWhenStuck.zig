//! zig_diff spec: func_800DB0BC -> detour_when_stuck.detourWhenStuck (71618.c).
//! Inputs: a stuckMovementState in the arena whose position and previous
//! x/z bytes are equal (the stuck case) or differ (the moved case), with any
//! detourStep (0, 1-4, 5 and the edges above it), random i16 x/z, and the
//! global D_800F5920 null or pointing at an arena block. The callees are
//! recording stubs: ratan2 returns angles whose low ten bits cover the whole
//! range (so the `> 512` half-turn test sees values below, at and above 512;
//! at exactly 512 the test is equivalent to `>=`, so the bound is probed at
//! 256 too), func_800E45D4 and func_800D954C return zero, both signs and
//! nonzero, and func_800D954C and func_800E4CE8/func_800E50A0 write the state
//! fields the C reads back (unkFB, unk15C, unk1C).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").detourWhenStuck));

pub const classes = [_][:0]const u8{ "global-set", "moved", "stuck-first-step", "stuck-mid-detour", "stuck-expired" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.stuckMovementState);

    if (ctx.chance(5)) {
        v.g.D_800F5920().* = ctx.alloc(v.D_800F5920_t);
    } else {
        v.g.D_800F5920().* = null;
    }

    st.position.p.x = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    st.position.p.z = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    st.previous.p.x = st.position.p.x;
    st.previous.p.z = st.position.p.z;
    if (ctx.chance(4)) st.previous.p.x +%= 1;
    if (ctx.chance(3)) st.previous.p.z +%= ctx.pick(u8, &.{ 1, 128, 255 });

    st.x = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 512 });
    st.z = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 512 });
    st.destination.p.x = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    st.destination.p.z = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    st.detourStep = ctx.pick(u8, &.{ 0, 0, 1, 2, 3, 4, 5, 6, 255 });
    st.unkFB = ctx.int(u8);
    st.unk1C = ctx.int(u8);

    return .{st};
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F5920().* != null) return 0;
    const st = args[0].?;
    if (st.position.raw & 0xFF00FF != st.previous.raw & 0xFF00FF) return 1;
    if (st.detourStep == 0) return 2;
    if (st.detourStep >= 5) return 4;
    return 3;
}

fn stateOf(p: anytype) *v.stuckMovementState {
    const raw: ?*anyopaque = @ptrCast(p);
    return @ptrCast(@alignCast(raw.?));
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        // Low ten bits uniform over 0..1023, quadrant bits random: every
        // half-turn comparison is reached on both sides.
        const r: c_int = ctx.value(u16) & 0xFFF;
        if (ctx.value(u8) < 40) return 512 + (r & ~@as(c_int, 0x3FF));
        return r;
    }

    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        const r = ctx.value(u8);
        if (r < 64) return 0;
        if (r < 128) return 1;
        if (r < 192) return -1;
        return 256;
    }

    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, probe: anytype) c_int {
        _ = probe;
        const s = stateOf(state);
        const r = ctx.value(u8);
        if (ctx.value(u8) < 30) s.unkFB = ctx.value(u8);
        if (r < 60) return 0;
        if (r < 100) return -1;
        return 1;
    }

    pub fn func_800E4CE8(ctx: *zd.Ctx, p: anytype) void {
        const s = stateOf(p);
        if (ctx.value(u8) < 128) s.unk15C = ctx.value(u8);
    }

    pub fn func_800E50A0(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const s = stateOf(p);
        s.unk1C = ctx.value(u8);
    }
};
