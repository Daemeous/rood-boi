//! zig_diff spec: vs_battle_applyCameraState -> apply_camera_state.applyCameraState (4A0A8.c).
//! Inputs: the camera object D_800F4BA4 (in the arena, every flag and field
//! random), D_800F4BA0 init-or-not, the look-at/eye vectors, the tracking
//! flag (unk1A4, as a signed short, zero and both signs), the offset flag
//! (unk1E8 equal to or different from 0x2000), the five request flags
//! (zero, 1, 0x80, 0xFF), and the D_800F4C6A byte. The callees that write
//! state the C reads back (the setters, _vectorAdd, the matrix calls) are
//! modelled; the setters copy their vector arguments into the object so a
//! wrong local shows up in the state (vx, vy and vz only: the C's stack
//! vectors' pad words are never written, so they are uninitialised there).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").applyCameraState));

pub const classes = [_][:0]const u8{ "init", "plain", "offset", "tracking" };

fn flag(ctx: *zd.Ctx) u8 {
    return ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
}

/// A second camera block the position setter may point D_800F4BA4 at (the C reads the
/// pointer again after every call).
var other: ?*v.D_800F4BA4_t = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = d;
    other = ctx.alloc(v.D_800F4BA4_t);
    const mode = ctx.range(u8, 0, 3);
    v.g.D_800F4BA0().* = if (mode == 0) ctx.edge(u8, &.{ 1, 0x80, 0xFF }) else 0;
    v.g.D_800F4C6A().* = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    d.unk1E8 = if (ctx.chance(2)) 0x2000 else ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x2001 });
    d.unk1A4.unk0.s16 = if (ctx.chance(2)) 0 else ctx.edge(i16, &.{ 1, -1, -32768, 32767 });
    d.unk168.unk0 = flag(ctx);
    d.unk174.unk0 = flag(ctx);
    d.projectionDistance.unk0 = flag(ctx);
    d.nearClip.unk0 = flag(ctx);
    d.farClip.unk0 = flag(ctx);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.D_800F4BA4().*.?;
    if (v.g.D_800F4BA0().* != 0) return 0;
    if (d.unk1A4.unk0.s16 != 0) return 3;
    return if (d.unk1E8 == 0x2000) 1 else 2;
}

pub const stubs = struct {
    fn fillVec(ctx: *zd.Ctx, p: anytype) void {
        const o: *v.VECTOR = @ptrCast(@alignCast(p));
        o.vx = ctx.value(i32);
        o.vy = ctx.value(i32);
        o.vz = ctx.value(i32);
    }

    pub fn vs_battle_initialiseCameraFromSpherical(ctx: *zd.Ctx, eye: anytype, look: anytype, a: c_int, b: c_int) void {
        _ = a;
        _ = b;
        fillVec(ctx, eye);
        fillVec(ctx, look);
    }

    pub fn _vectorAdd(ctx: *zd.Ctx, out: anytype, a: anytype, b: anytype) [*c]c.VECTOR {
        _ = ctx;
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        const x: *const v.VECTOR = @ptrCast(@alignCast(a));
        const y: *const v.VECTOR = @ptrCast(@alignCast(b));
        o.vx = x.vx +% y.vx;
        o.vy = x.vy +% y.vy;
        o.vz = x.vz +% y.vz;
        return null;
    }

    pub fn _atan2FixedPoint(ctx: *zd.Ctx, a: c_int, b: c_int) c_int {
        _ = a;
        _ = b;
        return ctx.value(c_int);
    }

    pub fn RotMatrix_gte(ctx: *zd.Ctx, r: anytype, m: anytype) [*c]c.MATRIX {
        _ = r;
        const o: *v.MATRIX = @ptrCast(@alignCast(m));
        o.m[0][0] = ctx.value(i16);
        o.m[1][1] = ctx.value(i16);
        o.m[2][2] = ctx.value(i16);
        o.t[0] = ctx.value(i32);
        o.t[1] = ctx.value(i32);
        o.t[2] = ctx.value(i32);
        return null;
    }

    pub fn ApplyMatrixLV(ctx: *zd.Ctx, m: anytype, v0: anytype, v1: anytype) [*c]c.VECTOR {
        _ = m;
        _ = v0;
        fillVec(ctx, v1);
        return null;
    }

    pub fn vs_battle_setCameraLookAt(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const q: *const v.VECTOR = @ptrCast(@alignCast(p));
        if (v.g.D_800F4BA4().*) |d| {
            d.unk27C.vx = q.vx;
            d.unk27C.vy = q.vy;
            d.unk27C.vz = q.vz;
        }
    }

    pub fn vs_battle_setCameraPosition(ctx: *zd.Ctx, p: anytype) void {
        const q: *const v.VECTOR = @ptrCast(@alignCast(p));
        if (v.g.D_800F4BA4().*) |d| {
            d.eye.target.vx = q.vx;
            d.eye.target.vy = q.vy;
            d.eye.target.vz = q.vz;
            if (ctx.value(u1) == 1) d.unk168.unk0 = ctx.value(u8);
            if (ctx.value(u3) == 0) v.g.D_800F4BA4().* = other.?;
        }
    }

    pub fn vs_battle_setCameraRoll(ctx: *zd.Ctx, a: c_int) void {
        _ = a;
        if (v.g.D_800F4BA4().*) |d| {
            if (ctx.value(u1) == 1) d.unk174.unk0 = ctx.value(u8);
        }
    }

    pub fn vs_battle_setProjectionDistance(ctx: *zd.Ctx, a: c_int) void {
        _ = a;
        if (v.g.D_800F4BA4().*) |d| {
            if (ctx.value(u1) == 1) d.nearClip.unk0 = ctx.value(u8);
        }
    }

    pub fn vs_battle_setNearClip(ctx: *zd.Ctx, a: c_int) void {
        _ = a;
        if (v.g.D_800F4BA4().*) |d| {
            if (ctx.value(u1) == 1) d.farClip.unk0 = ctx.value(u8);
        }
    }
};
