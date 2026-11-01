//! zig_diff spec: func_800B0908 -> func_800B0908.func_800B0908 (44F14.c).
//! Inputs: one objectAnimationState in the arena with a random delay, rotation
//! ticks and angular speeds (signed and zero), every mode the C tests (0xFF to
//! 0xF7, 0 and the generic values, including 1 with parent 0xFF), dx/dy/dz on
//! their snap edges (-3..3, -1, 2, extremes), and positions aligned to 0x40 on
//! x and z half the time. The sixteen D_800F45E0 slots are set: slot 0 is null
//! sometimes, slot 5 sometimes aliases the object, the others are arena children
//! whose flags, parent, height and material are biased toward the C's tests.
//! The callees return the scratch height (equal to position.vy often, and
//! near it), 0 and nonzero flags, and a child index 1..15 or 0; the animation
//! stub may set the frame, and the 0xFD callee may set the material.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B0908));

pub const classes = [_][:0]const u8{ "delayed", "idle", "ff", "fe", "fd", "fall", "tail", "generic" };

const edges16 = [_]i16{ 0, 1, -1, 2, -2, 3, -3, 5, -5, 100, -100, 0x7FFF, -32768 };
const modes = [_]i16{ 0, 1, 2, -1, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const o = ctx.alloc(v.objectAnimationState);
    o.id = ctx.int(u8);
    o.delay = if (ctx.chance(3)) ctx.pick(i8, &.{ 1, -1, 127, -128 }) else 0;
    o.rotationTicks = ctx.pick(i16, &.{ 0, 1, -1, 2, 3, 0x7FFF, -32768 });
    o.drx = ctx.pick(i16, &edges16);
    o.dry = ctx.pick(i16, &edges16);
    o.drz = ctx.pick(i16, &edges16);
    o.mode = ctx.pick(i16, &modes);
    o.parent = if (ctx.chance(2)) 0xFF else ctx.int(u8);
    o.dx = ctx.pick(i16, &edges16);
    o.dz = ctx.pick(i16, &edges16);
    // Mode 0xFF falls (and reaches the later `step == 0xFF` test) only with
    // dx and dz both zero: about 1 in 17,000 from independent picks.
    if (o.mode == 0xFF and ctx.chance(3)) {
        o.dx = 0;
        o.dz = 0;
    }
    o.dy = if (ctx.chance(2)) ctx.range(i16, -3, 0) else ctx.pick(i16, &edges16);
    o.orientation = ctx.pick(i16, &.{ 0, 1, 2, 3, -1, -2, 4, -3 });
    o.position.vx = ctx.int(i16);
    o.position.vz = ctx.int(i16);
    o.position.vy = ctx.pick(i16, &.{ 0, -1, 1, 127, -128, 0x7FFF, -32768 });
    if (ctx.chance(2)) o.position.vx = (o.position.vx & ~@as(i16, 0x7F)) | 0x40;
    if (ctx.chance(2)) o.position.vz = (o.position.vz & ~@as(i16, 0x7F)) | 0x40;
    o.material = ctx.pick(u8, &.{ 1, 5, 6, 7, 0x15, 0x35, 0x26, 0x17, 0x20 });
    o.durability = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    o.frame = ctx.int(u8);
    o.tileX = ctx.int(u8);
    o.tileZ = ctx.int(u8);
    o.flags = ctx.int(i32);

    const table = v.g.D_800F45E0();
    for (table, 0..) |*slot, i| {
        if (i == 0 and ctx.chance(2)) {
            slot.* = null;
            continue;
        }
        if (i == 5 and ctx.chance(8)) {
            slot.* = @ptrCast(o);
            continue;
        }
        const child = ctx.alloc(v.objectAnimationState);
        child.flags = ctx.pick(i32, &.{ 0, 1, 2, 3 });
        child.parentObject = if (ctx.chance(2)) o.id else ctx.int(u8);
        child.position.vy = ctx.int(i16);
        child.material = if (ctx.chance(2)) o.material else ctx.int(u8);
        slot.* = @ptrCast(child);
    }

    return .{ o, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const o = args[0].?;
    if (o.delay != 0) return 0;
    return switch (o.mode) {
        0 => 1,
        0xFF => 2,
        0xFE => 3,
        0xFD => 4,
        0xFC, 0xF9 => 5,
        0xFB, 0xFA, 0xF8, 0xF7 => 6,
        else => 7,
    };
}

pub const stubs = struct {
    pub fn vs_main_panSfx(ctx: *zd.Ctx, a: anytype, b: anytype, vec: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        _ = vec;
    }

    pub fn func_800E4C28(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
    }

    pub fn func_800E6898(ctx: *zd.Ctx, p: ?*anyopaque) void {
        const o: *v.objectAnimationState = @ptrCast(@alignCast(p.?));
        if (ctx.value(u8) < 64) o.material = ctx.value(u8);
    }

    pub fn func_800A3DB4(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        const sv: *v.SVECTOR = @ptrCast(@alignCast(p));
        const r = ctx.value(u8);
        if (r < 40) sv.vy = ctx.value(i16);
        if (r < 160) return @as(c_int, sv.vy) + @as(c_int, ctx.value(u2));
        return ctx.value(c_int);
    }

    pub fn func_800A91DC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u8) < 96) return 0;
        return 1 + @as(c_int, ctx.value(u8) % 15);
    }

    pub fn func_800B101C(ctx: *zd.Ctx, object: anytype) c_int {
        const o: *v.objectAnimationState = @ptrCast(@alignCast(object));
        if (ctx.value(u4) < 4) o.frame = ctx.value(u8);
        return if (ctx.value(u1) == 1) 1 else 0;
    }
};
