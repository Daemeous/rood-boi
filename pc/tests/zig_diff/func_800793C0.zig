//! zig_diff spec: func_800793C0 -> func_800793C0.func_800793C0 (146C.c).
//! Inputs: D_800F19EC 0..3; the projection distance at, around and 64 off
//! 512; the camera distance against unk2C0C (equal, within and beyond 192);
//! D_800F19D8 equal to unk2C0C or not; the look-at arrived or not (stub);
//! 0..5 chain links (unk4) of 0..4 hits each, unk40 0 or not; the busy
//! stub (func_8009E4B0) -1, 0 (idle) or busy; the battle state 3 or not;
//! func_800BEC58's answer 1 or not.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800793C0));

pub const classes = [_][:0]const u8{ "easing", "settled" };

pub const stubs = struct {
    pub fn _isLookAtAtDestination(ctx: *zd.Ctx) c_int {
        return if (ctx.value(u2) == 0) 0 else 1;
    }
    pub fn func_8009E4B0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return if (ctx.value(u3) == 0) ctx.edge(c_uint, &.{ 1, 2, 0xFFFF_FFFE }) else ctx.edge(c_uint, &.{ 0, 0xFFFF_FFFF });
    }
    pub fn func_800BEC58(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_short {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        return ctx.edge(c_short, &.{ 0, 1, 2 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    psx.global(c_int, "D_800F19EC").* = ctx.range(c_int, 0, 3);
    v.g.vs_main_projectionDistance().* = ctx.edge(i32, &.{ 512, 512, 511, 513, 448, 449, 576, 575, 577, 447, 300, -100, 0x7FFF_FFF0 });
    block.unk2C0C = ctx.pick(i16, &.{ 0, 1024, 2000 });
    const camera = &v.g.vs_battle_cameraCurrentSpherical().values;
    camera.distance = block.unk2C0C +% ctx.edge(i32, &.{ 0, 0, 1, -1, 191, 192, 193, -191, -192, -193 });
    psx.global(c_int, "D_800F19D8").* = if (ctx.chance(4)) 7 else block.unk2C0C;
    block.unk4 = ctx.edge(u32, &.{ 0, 1, 2, 5 });
    for (&block.unk854) |*chain| {
        chain.unk4A = ctx.range(u16, 0, 4);
        chain.unk4.unk40 = if (ctx.chance(3)) 1 else 0;
        for (0..4) |h| chain.unk4C[h].unk40 = if (ctx.chance(3)) 1 else 0;
    }
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 3, 2, 1 });
    return .{};
}

pub fn classify(_: Args) usize {
    return if (v.g.vs_main_projectionDistance().* == 512) 1 else 0;
}
