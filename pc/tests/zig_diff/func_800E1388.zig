//! zig_diff spec: func_800E1388 -> func_800E1388.func_800E1388 (771A8.c).
//! Inputs: a state object in the arena and a bounds block that the global
//! D_800F58BC points at, with random bounds that are sometimes pinned to the
//! source position so the clamps see their sides equal. The source is any
//! packed movement position. The distance is mostly small (so the rotated
//! offset stays near the source cell and the clamp comparisons hit equality),
//! sometimes wide (so the 32-bit product wraps and goes negative). The angle
//! takes edge values. rcos and rsin return signed bytes; func_800D954C
//! returns a break (>0), a zero, or a negative, and sometimes rewrites the
//! bounds block that the C reads back after the loop.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1388));

pub const classes = [_][:0]const u8{ "wide-distance", "small-bound-at-source", "small-distance" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.func_800D8400_t);
    const bounds = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = bounds;

    var source: v.vs_battle_movementPosition = undefined;
    source.raw = ctx.int(u32);
    const sx = ctx.int(u8);
    const sz = ctx.int(u8);
    psx.setBits(&source.p, "x", sx);
    psx.setBits(&source.p, "z", sz);

    bounds.minX = ctx.int(u8);
    bounds.maxX = ctx.int(u8);
    bounds.minZ = ctx.int(u8);
    bounds.maxZ = ctx.int(u8);
    if (ctx.chance(3)) bounds.minX = sx;
    if (ctx.chance(3)) bounds.maxX = sx;
    if (ctx.chance(3)) bounds.minZ = sz;
    if (ctx.chance(3)) bounds.maxZ = sz;

    const angle = ctx.pick(c_int, &.{ 0, 0xC00, -0xC00, 0x7FFFFFFF, -2147483648, 1, -1 });
    const distance: c_uint = switch (ctx.range(u8, 0, 3)) {
        0 => ctx.range(c_uint, 0, 4095),
        1 => ctx.pick(c_uint, &.{ 0, 1, 7, 8, 4095, 4096 }),
        2 => ctx.pick(c_uint, &.{ 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF }),
        else => ctx.int(c_uint),
    };

    return .{ state, source, angle, distance };
}

pub fn classify(args: Args) usize {
    if (args[3] >= 4096) return 0;
    const b = v.g.D_800F58BC().*.?;
    const sx = args[1].raw & 0xFF;
    const sz = (args[1].raw >> 16) & 0xFF;
    if (b.minX == sx or b.maxX == sx or b.minZ == sz or b.maxZ == sz) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        return ctx.value(i8);
    }

    pub fn rsin(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        return ctx.value(i8);
    }

    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, raw: anytype) c_int {
        _ = state;
        _ = raw;
        const r = ctx.value(u8);
        // The C re-reads the bounds after the loop: the callee may move them.
        if (ctx.value(u8) < 26) {
            const b = v.g.D_800F58BC().*.?;
            b.minX = ctx.value(u8);
        }
        if (r < 64) return 1; // break out of the loop
        if (r < 160) return 0; // not a break; the value the C tests against 0
        return -1;
    }
};
