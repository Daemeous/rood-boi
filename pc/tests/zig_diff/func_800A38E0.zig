//! zig_diff spec: func_800A38E0 -> func_800A38E0.func_800A38E0 (3A1A0.c).
//! Inputs: an actorId in 0..16 whose table slots all hold one arena actor (or
//! all null, sometimes). The actor's facing, tile, flag bytes (0x5AC and the
//! flags word at 8), position and object-id are biased to the edge values the
//! C tests. The object (shared by all 16 object slots) has a random 0x16C byte,
//! unk1A and unk1E, with vy set to unk1E + 128, unk1E - 64, 384 or random so
//! the ordered comparisons see equal sides. The callees are recording stubs;
//! they return 0 or nonzero, a random object id 0..15, and write the actor's
//! position and the object's fields so the C has to re-read them. The floor
//! stub func_800A3BC4 returns exactly the actor's vy - 384 most of the time,
//! so the `direction < func_800A3BC4(...)` compares see equal sides.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A38E0));

pub const classes = [_][:0]const u8{ "null-or-busy", "fail-flags", "object-id-branch", "found-branch" };

/// The actor and object the current iteration built (stubs write them).
var g_actor: ?*v.D_800F4538_t = null;
var g_object: ?*v.D_800F45E0_t = null;

pub const stubs = struct {
    pub fn func_800A3DB4(ctx: *zd.Ctx, x: anytype, z: anytype, y: anytype) c_int {
        _ = x;
        _ = z;
        _ = y;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800A3BC4(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        if (ctx.value(u2) == 0) return ctx.value(i16);
        // Equal to the C's `direction` (vy - 384) most of the time.
        return @as(c_int, g_actor.?.unk0.position.vy) - 384;
    }

    pub fn func_800A91DC(ctx: *zd.Ctx, x: anytype, z: anytype, flag: anytype) c_int {
        _ = x;
        _ = z;
        _ = flag;
        if (ctx.value(u1) == 1) g_actor.?.unk0.position.vy = ctx.value(i16);
        if (ctx.value(u3) == 0) return 0;
        if (ctx.value(u2) == 0) g_object.?.unk1E = ctx.value(i16);
        return @as(c_int, ctx.value(u4));
    }

    pub fn func_800A3C00(ctx: *zd.Ctx, object: anytype, flag: anytype) ?*c.D_800F4538_t {
        _ = object;
        _ = flag;
        if (ctx.value(u1) == 1) g_object.?.unk1A = ctx.value(i16);
        if (ctx.value(u2) == 0) return null;
        return @ptrCast(&zd.zdiff_arena[0]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const object = ctx.alloc(v.D_800F45E0_t);
    g_actor = actor;
    g_object = object;

    actor.unk0.facing = ctx.pick(i16, &.{ -512, -1, 0, 511, 512, 1023, 0x7FFF, -32768 });
    actor.unk0.currentTileX = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    actor.unk0.currentTileZ = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    actor.unk0.unk13 = ctx.range(u8, 0, 15);
    actor.unk0._bits_8[2] = ctx.pick(u8, &.{ 0x00, 0x08, 0x10, 0x18, 0x01, 0x04, 0x07, 0x80, 0xE0 });
    actor._bits_1452[0] = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 8, 12, 0x80, 0xFF });

    actor.unk0.position.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 384 });
    actor.unk0.position.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 384 });

    const e = ctx.pick(i16, &.{ 0, 1, -1, 300, -300, 32767, -32768 });
    object.unk1E = e;
    object.unk1A = if (ctx.chance(4)) 1 else 0;
    psx.byteAt(u8, object, 0x16C).* = ctx.pick(u8, &.{ 0x00, 0x10, 0x20, 0x30, 0x04, 0x03, 0x07, 0x14 });
    actor.unk0.position.vy = switch (ctx.range(u2, 0, 3)) {
        0 => @as(i16, @truncate(@as(i32, e) + 128)),
        1 => e -% 64,
        2 => 384,
        else => ctx.int(i16),
    };

    const null_actor = ctx.chance(8);
    for (v.g.D_800F4538()) |*slot| slot.* = if (null_actor) null else actor;
    for (v.g.D_800F45E0()) |*slot| slot.* = object;

    return .{ctx.range(c_int, 0, 16)};
}

pub fn classify(args: Args) usize {
    const actor = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    const flags = std.mem.readInt(u32, actor.unk0._bits_8[0..4], .little);
    if (((actor._bits_1452[0] & 3) != 0) or ((flags & 0x70000) != 0)) return 0;
    if ((flags & 0x180000) != 0) return 1;
    if ((actor._bits_1452[0] & 8) != 0) return 2;
    return 3;
}
