//! zig_diff spec: func_800DA738 -> func_800DA738.func_800DA738 (71618.c).
//! Inputs: a da4bcState in a 4 KB arena block (the state sits 0x400 in, so
//! negative targets index inside the block), a shared D_800F4538 model, 16
//! other states that the scratchpad slots 0..15 point at, and the scratchpad
//! slots for every target from -128 to 127. The target is out of range, equal
//! to the state's id, 16, empty, inactive, active without bit 5, or active
//! with bit 5. The stubs choose the candidate sign and value, the tables
//! for rcos/rsin make dot land exactly on -1200 now and then, and the
//! callees' return values and state writes (timer44, target16) vary per call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DA738));

pub const classes = [_][:0]const u8{
    "self-or-16",
    "out-of-range",
    "empty-slot",
    "inactive",
    "active-no-bit5",
    "active-bit5",
};

const slot_base: isize = 0x1F80_037C;

fn slotAddr(k: isize) usize {
    return @intCast(slot_base + 4 * k);
}

fn slotPtr(k: isize) *align(1) u32 {
    return @ptrFromInt(slotAddr(k));
}

fn asT(comptime T: type, p: anytype) *T {
    const raw = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(raw));
}

fn flagResult(ctx: *zd.Ctx) c_int {
    return if (ctx.value(u8) < 128) 0 else ctx.value(c_int) | 1;
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return ctx.value(c_int);
    }

    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        const pool = [_]c_int{ 4096, -1200, 4096, -1200, 0, 1 };
        return pool[ctx.value(u8) % pool.len];
    }

    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        const pool = [_]c_int{ 0, 0, 0, 1, -1, 4096, -4096 };
        return pool[ctx.value(u8) % pool.len];
    }

    pub fn func_800DBB2C(ctx: *zd.Ctx, state: anytype, point: anytype, n: anytype) c_int {
        _ = n;
        const st = asT(v.da4bcState, state);
        const pt = asT(v.da4bcPoint, point);
        pt.b[0] = ctx.value(u8);
        pt.b[2] = ctx.value(u8);
        if (ctx.value(u8) < 64) psx.setBits(st, "target16", @intCast(ctx.value(u4)));
        const k = ctx.value(u8);
        if (k < 80) return -1;
        if (k < 96) return std.math.minInt(c_int);
        return @intCast(k % 16);
    }

    pub fn func_800E1388(ctx: *zd.Ctx, s: anytype, tile: anytype, angle: anytype, flags: anytype) void {
        _ = tile;
        _ = angle;
        _ = flags;
        asT(v.da4bcState, s).flagE = ctx.value(u8);
    }

    pub fn func_800E50A0(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const st = asT(v.da4bcState, s);
        st.timer44 = if (ctx.value(u1) == 1) 0 else ctx.value(u8);
    }

    pub fn func_800E2CCC(ctx: *zd.Ctx, s: anytype) void {
        asT(v.da4bcState, s).flagE = ctx.value(u8);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, s: anytype, t: anytype, a: anytype, b: anytype) c_int {
        _ = s;
        _ = t;
        _ = a;
        _ = b;
        return flagResult(ctx);
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, s: anytype, t: anytype) c_int {
        _ = s;
        _ = t;
        return flagResult(ctx);
    }

    pub fn func_800DB61C(ctx: *zd.Ctx, s: anytype, n: anytype) c_int {
        _ = s;
        _ = n;
        return flagResult(ctx);
    }

    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return switch (ctx.value(u8) % 4) {
            0 => 128,
            1 => -128,
            2 => 127,
            else => ctx.value(c_int),
        };
    }

    pub fn func_800D8260(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        asT(v.da4bcState, s).pending = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x1000, 16);
    const state: *v.da4bcState = @ptrCast(@alignCast(block + 0x400));

    // The model every D_800F4538 entry points at.
    const model = ctx.alloc(v.D_800F4538_t);
    const mdl: *v.da4bcModel = @ptrCast(@alignCast(model));
    mdl.x = ctx.pick(i16, &.{ 64, -64, 0, 32767, -32768 });
    mdl.z = ctx.pick(i16, &.{ 64, -64, 0, 32767, -32768 });
    mdl.tile.raw = ctx.pick(u32, &.{ 0, 0x00FF_00FF, 0x0000_0100, 0xFFFF_FFFF });
    for (v.g.D_800F4538()) |*e| e.* = model;

    // Sixteen other states; their distances are the edge values of <= 0x23FFF.
    var others: [16]*v.da4bcState = undefined;
    for (&others) |*o| {
        const obj = ctx.alloc(v.da4bcState);
        o.* = obj;
        for (&obj.distances) |*d| {
            d.* = ctx.pick(u32, &.{ 0x23FFF, 0x23FFF, 0x24000, 0x23FFE, 0, 0xFFFF_FFFF });
        }
    }

    // Every scratchpad slot from -128 to 127: 0..15 point at a state (or
    // are empty), the rest are empty.
    var k: isize = -128;
    while (k < 128) : (k += 1) {
        var val: u32 = 0;
        if (k >= 0 and k < 16 and !ctx.chance(5)) val = psx.addr(others[@intCast(k)]);
        slotPtr(k).* = val;
    }

    const mode = ctx.range(u8, 0, 7);
    var target: i8 = 16;
    if (mode == 2) {
        target = if (ctx.chance(2)) ctx.range(i8, -128, -1) else ctx.range(i8, 17, 127);
    } else if (mode != 0) {
        target = @intCast(ctx.range(u8, 0, 15));
    }
    psx.setBits(state, "target16", target);
    state.id = if (mode == 1) @intCast(target) else 200;
    state.timer44 = ctx.pick(u8, &.{ 0, 1, 2, 255 });

    if (target >= 0 and target < 16) {
        const idx: usize = @intCast(target);
        const slot: u32 = switch (mode) {
            3 => 0,
            // Nonzero with the sign bit set: the C never dereferences it,
            // because this target's bits say it is not active.
            4 => ctx.int(u32) | 0x8000_0000,
            else => psx.addr(others[idx]),
        };
        slotPtr(@intCast(target)).* = slot;
        const t = state.targets[idx];
        state.targets[idx] = switch (mode) {
            4 => (t & ~@as(u32, 0xC0)) | 0x40,
            5 => (t & ~@as(u32, 0x60)) | 0x80,
            6, 7 => (t & ~@as(u32, 0x60)) | 0xA0,
            else => t,
        };
    }

    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const id: i32 = state.id;
    const target: i32 = psx.getBits(state, "target16");
    if (target == 16 or id == target) return 0;
    if (target < 0 or target > 15) return 1;
    const slot = slotPtr(@intCast(target)).*;
    if (slot == 0) return 2;
    const t = state.targets[@intCast(target)];
    if ((@as(u8, @truncate(t)) >> 6) != 2) return 3;
    if (((t >> 5) & 1) == 0) return 4;
    return 5;
}
