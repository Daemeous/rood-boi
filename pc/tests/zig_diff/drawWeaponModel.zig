//! zig_diff spec: func_800B217C -> weapon_model.drawWeaponModel (49268.c).
//! Inputs: a weapon model in the arena (nBones 0-4, parent bones 0-3, unk11/unk12/unk13
//! at the edges so the step lands on 64 or on 0 and the clutSlotOffset mask test goes
//! both ways), its owner actor in D_800F4538[actorId] (visible or hidden, position.vy
//! around 256 and negative, unkF zero or not, the flags nibble 0-15 covering the
//! unsigned wrap at 0-3), a source matrix, a small nearClip. The matrix helpers write
//! their outputs; func_800BEB00 returns 4 or other values; func_800B3A68 returns 0 or
//! nonzero. Caveat: the bone loop stores D_800F3C58[i] for i >= 1, past the one-entry
//! room. nBones stays at 4 or less, so those stores spill at most 128 bytes into the
//! state region, which both runs write identically and the compare covers.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_49268");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawWeaponModel));

pub const classes = [_][:0]const u8{ "hidden", "far-unkF-zero", "far-unkF-set", "near-nibble-under-4", "near-rest" };

fn fillMatrix(ctx: *zd.Ctx, mat: *v.MATRIX) void {
    for (&mat.m) |*row| {
        for (row) |*e| e.* = ctx.value(i16);
    }
    for (&mat.t) |*e| e.* = ctx.value(i32);
}

pub const stubs = struct {
    pub fn func_800B396C(ctx: *zd.Ctx, m: anytype, s: anytype) void {
        _ = s;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        fillMatrix(ctx, mat);
    }
    pub fn func_800B17F0(ctx: *zd.Ctx, r: anytype, m: anytype) void {
        _ = r;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        fillMatrix(ctx, mat);
    }
    pub fn func_800B196C(ctx: *zd.Ctx, parent: anytype, m: anytype) void {
        _ = parent;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        fillMatrix(ctx, mat);
    }
    pub fn func_800B3A68(ctx: *zd.Ctx, w: anytype, m: anytype, n: anytype, p: anytype) c_int {
        _ = w;
        _ = m;
        _ = n;
        _ = p;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int);
    }
    // The drawers read the colour local (a stack pointer the trace only tags):
    // they copy its r, g, b into their first argument (D_800F2458, compared
    // state), and its code byte when the colour came whole from the actor
    // (with weapon flag 0x40 the C leaves color.cd uninitialised).
    pub fn func_800B516C(ctx: *zd.Ctx, out: anytype, w: anytype, color: anytype, model: anytype) void {
        _ = ctx;
        _ = model;
        drawn(out, w, color, 0);
    }
    pub fn func_800B3CF0(ctx: *zd.Ctx, out: anytype, w: anytype, color: anytype, model: anytype) void {
        _ = ctx;
        _ = model;
        drawn(out, w, color, 4);
    }
    pub fn func_800BEB00(ctx: *zd.Ctx) c_short {
        if (ctx.value(u8) < 128) return 4;
        return ctx.value(c_short);
    }
};

fn drawn(out: anytype, w: anytype, color: anytype, at: usize) void {
    const dst: [*]u8 = @ptrCast(out);
    const src: [*]const u8 = @ptrCast(color);
    const weapon: *const v.vs_battle_wepModels_t = @ptrCast(@alignCast(w));
    dst[at + 0] = src[0];
    dst[at + 1] = src[1];
    dst[at + 2] = src[2];
    if (weapon._bits_8[0] & 0x40 == 0) dst[at + 3] = src[3];
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const weapon = ctx.alloc(v.vs_battle_wepModels_t);
    const actor = ctx.alloc(v.D_800F4538_t);
    const source = ctx.alloc(v.MATRIX);
    // The bone bytes live in the offsets block (scratch->model is weapon->offsets).
    const model = ctx.allocBytes(8192, 16);
    const offsets: *v._wepModelOffsets = @ptrCast(@alignCast(model));
    psx.put(&weapon.offsets, offsets);

    weapon.nBones = ctx.range(u8, 0, 4);
    for (1..4) |i| model[i * 16 + 0x44] = ctx.range(u8, 0, 3);

    weapon.texSlot = ctx.range(u8, 0, 19);
    weapon.clutSlotOffset = ctx.range(u8, 0, 19);
    weapon.unk11 = ctx.pick(u8, &.{ 64, 0, 255, 1, 63 });
    weapon.unk12 = ctx.pick(u8, &.{ 0, 64, 255, 1 });
    weapon.unk13 = ctx.pick(u8, &.{ 0, 1, 2, 128, 129, 255 });
    if (ctx.chance(4)) {
        weapon.unk13 = 1;
        weapon.unk12 = 0;
    }

    const id = ctx.range(u8, 0, 15);
    weapon._bits_8[0] = (ctx.int(u8) & 0xB0) | (if (ctx.chance(2)) @as(u8, 0x40) else 0) | id;
    v.g.D_800F4538().*[id] = actor;

    actor.unk0._bits_8[0] = (ctx.int(u8) & 0xFD) | (if (ctx.chance(4)) @as(u8, 0) else 2);
    actor.unk0._bits_8[1] = (ctx.int(u8) & 0xF0) | ctx.range(u8, 0, 15);
    actor.unk0.unkF = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 255);
    actor.unk0.position.vy = ctx.pick(i16, &.{ 255, 256, 257, -1, 0, 32767, -32768 });

    v.g.vs_main_nearClip().* = ctx.range(i32, -20000, 20000);
    return .{ weapon, source };
}

pub fn classify(args: Args) usize {
    const weapon = args[0].?;
    const id: usize = weapon._bits_8[0] & 15;
    const actor = v.g.D_800F4538()[id].?;
    if ((actor.unk0._bits_8[0] & 2) == 0) return 0;
    if (actor.unk0.position.vy < 256) {
        return if ((actor.unk0._bits_8[1] & 15) < 4) 3 else 4;
    }
    return if (actor.unk0.unkF == 0) 1 else 2;
}
