//! zig_diff spec: func_800AC690 -> func_800AC690.func_800AC690 (40564.c).
//! Inputs: arg0 0 (the player) or another slot; a fresh actor whose flag
//! word at 8 holds the airborne state (2, 3, 4 or another) with the profile
//! bits 0xF00/0x400000/0x200000 (and 0x5AC bit 3, unk5B0's low bits), whose
//! unk11 timer is around 0 (signed), unk181A 0..3 with unk1816 around 0,
//! the slide limits unk1814/unk1818 around the next position, vy around 0,
//! position near 0 and the floor. The tick speed is 2 or 4. D_800F49F4 is 0,
//! the actor's tile byte unk5EC[1] or another contact; D_800F49F8 0 or a
//! platform slot with an arena object (unk1A zero or not); D_800F49F0 points
//! at a word whose low five bits index D_800E9278 (20 or not). The floor
//! stubs return values around the actor's height, func_800A6EE8 3000 at
//! times.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC690));

pub const classes = [_][:0]const u8{ "windup", "takeoff", "flight", "recovery", "other" };

var height: i16 = 0;
var actor_g: *v.D_800F4538_t = undefined;

/// The actor's height plus its (already stepped) vertical speed.
fn top() c_int {
    return @as(c_int, actor_g.unk0.position.vy) + actor_g.unk0.unk34.vy;
}

pub const stubs = struct {
    pub fn func_8008DD0C(ctx: *zd.Ctx, x: anytype, z: anytype) c_short {
        _ = x;
        _ = z;
        return switch (ctx.value(u3)) {
            0 => ctx.value(i16),
            1 => height,
            2 => @truncate(top() - 0xBF),
            3 => @truncate(top() - 0xC0),
            else => height +% @as(i16, ctx.value(i8)),
        };
    }

    pub fn func_800A9378(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype, mode: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        _ = mode;
        return switch (ctx.value(u3)) {
            0 => 0,
            1 => ctx.value(i16),
            2, 3 => height,
            4 => 2999,
            else => @as(c_int, height) + ctx.value(i8),
        };
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, vx: anytype, vz: anytype, mode: anytype) c_int {
        _ = p;
        _ = vx;
        _ = vz;
        _ = mode;
        return switch (ctx.value(u3)) {
            0 => 3000,
            1 => 2999,
            2 => top(),
            3 => top() - 0x80,
            4 => @as(c_int, height) - 0x80 + ctx.value(i8),
            else => @as(c_int, height) + ctx.value(i8),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    actor_g = actor;
    const arg0: c_int = if (ctx.chance(2)) 0 else ctx.range(c_int, 1, 15);
    var flags = ctx.int(u32);
    flags = (flags & ~@as(u32, 0x70000)) | (@as(u32, ctx.edge(u3, &.{ 2, 3, 3, 3, 4, 1 })) << 16);
    if (ctx.chance(2)) flags &= ~@as(u32, 0xF00);
    if (ctx.chance(3)) flags &= ~@as(u32, 0x400000);
    if (ctx.chance(3)) flags &= ~@as(u32, 0x200000);
    psx.byteAt(u32, actor, 8).* = flags;
    psx.byteAt(u32, actor, 0x5AC).* = ctx.int(u32);
    // The hover timer (top byte of the word at 0x5B4): edge values so `timer + y` wraps past 255 and `y * 2` is odd or even.
    psx.byteAt(u8, actor, 0x5B7).* = ctx.pick(u8, &.{ 0, 1, 0x7E, 0x7F, 0x80, 0xFE, 0xFF });
    actor.unk0.unk11 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 0xFF, 0x80 });
    actor.unk0.unkF = ctx.edge(u8, &.{ 0, 1, 7 });
    actor.unk181A = ctx.edge(i16, &.{ 0, 1, 2, 2, 3 });
    actor.unk1816 = ctx.edge(i16, &.{ 0, 1, 2, -1 });
    actor.unk5CC = ctx.edge(u8, &.{ 0, 1 });
    height = ctx.pick(i16, &.{ 0, -1, 1, 0x40, -0x40 });
    actor.unk0.position.vx = ctx.pick(i16, &.{ 0, 127, 128, -128 });
    actor.unk0.position.vy = height +% ctx.pick(i16, &.{ 0, -0xC0, 0xC0, -1, 1 });
    actor.unk0.position.vz = ctx.pick(i16, &.{ 0, 127, 128, -128 });
    actor.unk0.unk34.vx = ctx.pick(i16, &.{ 0, 5, -5 });
    actor.unk0.unk34.vy = ctx.pick(i16, &.{ 0, -1, 1, -3, -6, -12, -15, 3, 6, -7, 7, -9, 9, -10, 10, -100, 100 });
    actor.unk0.unk34.vz = ctx.pick(i16, &.{ 0, 5, -5 });
    actor.unk1814 = actor.unk0.position.vx +% actor.unk0.unk34.vx +% ctx.pick(i16, &.{ 0, 1, -1 });
    actor.unk1818 = actor.unk0.position.vz +% actor.unk0.unk34.vz +% ctx.pick(i16, &.{ 0, 1, -1 });
    // ((SVECTOR*)&unk4C)->vy: the fall's start height (0..6 steps of 64 above).
    psx.byteAt(i16, &actor.unk0.unk4C, 2).* = actor.unk0.position.vy -% ctx.pick(i16, &.{ 0, 255, 256, 384, 383, 1000 });
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 2, 4, 4, 1 });
    // Landing exactly at height 0 (pos.vy + the stepped vy == 0) at times.
    if (ctx.chance(6)) {
        const step = @divTrunc(v.g.vs_gametime_tickspeed().*, 2) * 3;
        actor.unk0.position.vy = @truncate(-(@as(c_int, actor.unk0.unk34.vy) + step));
    }

    const tile: *[8]u8 = @ptrCast(&actor.unk5EC);
    v.g.D_800F49F4().* = switch (ctx.range(u8, 0, 3)) {
        0 => 0,
        1 => tile[1],
        else => ctx.range(i32, 0, 20),
    };
    v.g.D_800F49F8().* = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 15);
    for (v.g.D_800F45E0()) |*slot| slot.* = null;
    if (v.g.D_800F49F8().* != 0) {
        const object = ctx.alloc(v.D_800F45E0_t);
        object.unk1A = ctx.edge(i16, &.{ 0, 1 });
        v.g.D_800F45E0()[v.g.D_800F49F8().*] = object;
    }
    const ground = ctx.alloc(u32);
    ground.* = ctx.int(u32);
    v.g.D_800F49F0().* = @ptrCast(ground);
    for (0..32) |k| v.g.D_800E9278()[k] = if (ctx.chance(2)) 20 else ctx.int(u8);
    return .{ arg0, actor };
}

pub fn classify(args: Args) usize {
    const actor = args[1].?;
    const flags = psx.byteAt(u32, actor, 8).*;
    const timer: i8 = @bitCast(actor.unk0.unk11);
    return switch ((flags >> 16) & 7) {
        2 => if (timer > 0) 0 else 1,
        3 => 2,
        4 => 3,
        else => 4,
    };
}
