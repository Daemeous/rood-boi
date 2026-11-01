//! zig_diff spec: func_80093B68 -> func_80093B68.func_80093B68 (2842C.c).
//! Inputs: the actor (small, or with bits above the byte the queue's u8
//! field drops), the limb index (-1 or not; func_800A152C gives a negative,
//! 255 or a small limb), the value at the 999 clamp's edges with bit 31 set
//! or not, the flags with bit 7 and low bits. The queue (D_800F1BAC, 32
//! arena entries, D_800F227E -1..12) has entries of kind 1 for this actor,
//! with this limb and unk6 near the count. Each position stub's result and
//! the point it writes are chosen in prepare: off-screen x/y at 320/321 and
//! 240/241, negative (large as u_short). func_800962E4 hands out an arena
//! entry. D_800691E4 is "%d".
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093B68));

pub const classes = [_][:0]const u8{ "limb-fail", "anchor-fail", "screen-fail", "offscreen", "position-fail", "queued" };

var limb_result: c_int = 0;
var anchor_result: c_int = 0;
var screen_result: c_int = 0;
var screen: v.SVECTOR = undefined;
var position_result: c_int = 0;
var position: v.SVECTOR = undefined;

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, m: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = m;
        return limb_result;
    }
    pub fn func_800962E4(ctx: *zd.Ctx) *v.D_800F1BAC_t {
        return ctx.alloc(v.D_800F1BAC_t);
    }
    pub fn func_800A1AF8(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) c_int {
        _ = a;
        _ = l;
        _ = m;
        const p: [*]u16 = @ptrCast(@alignCast(out));
        for (0..3) |i| p[i] = ctx.value(u16);
        return anchor_result;
    }
    pub fn func_800A190C(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) c_int {
        _ = a;
        _ = m;
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        if (l == 255) {
            p.vx = ctx.value(i16);
            p.vy = ctx.value(i16);
            p.vz = ctx.value(i16);
            return anchor_result;
        }
        p.* = position;
        return position_result;
    }
    pub fn func_800A1280(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = l;
        _ = m;
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        p.* = screen;
        return screen_result;
    }
};

fn fail(ctx: *zd.Ctx, n: u32) c_int {
    return if (ctx.chance(n)) ctx.pick(c_int, &.{ 1, -1 }) | 1 else 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor: c_int = if (ctx.chance(8)) ctx.int(c_int) else ctx.range(c_int, 0, 3);
    const limb_index: c_int = if (ctx.chance(3)) -1 else ctx.range(c_int, 0, 5);
    limb_result = switch (ctx.range(u8, 0, 7)) {
        0 => ctx.edge(c_int, &.{ -1, -2147483648 }),
        1 => 255,
        2 => 0,
        else => ctx.range(c_int, 0, 3),
    };
    const limb: c_int = if (limb_index != -1) limb_result else 255;
    anchor_result = fail(ctx, 8);
    screen_result = fail(ctx, 6);
    position_result = fail(ctx, 8);
    screen = .{
        .vx = ctx.edge(i16, &.{ 0, 160, 319, 320, 321, -1, -32768 }),
        .vy = ctx.edge(i16, &.{ 0, 120, 239, 240, 241, -1, -32768 }),
        .vz = ctx.int(i16),
        .pad = ctx.int(i16),
    };
    if (ctx.chance(2)) {
        screen.vx = ctx.range(i16, 0, 320);
        screen.vy = ctx.range(i16, 0, 240);
    }
    position = .{ .vx = ctx.int(i16), .vy = ctx.int(i16), .vz = ctx.int(i16), .pad = ctx.int(i16) };

    const q: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(32 * @sizeOf(v.D_800F1BAC_t), 16)));
    const n = ctx.edge(i16, &.{ -1, 0, 1, 2, 3, 5, 8, 12 });
    for (0..32) |i| {
        const e = &q[i];
        e.unk8.u8[0] = if (ctx.chance(4)) ctx.int(u8) else 1;
        e.unk8.u8[1] = if (ctx.chance(4)) ctx.int(u8) else @truncate(@as(c_uint, @bitCast(actor)));
        e.unkA = if (ctx.chance(3)) ctx.int(u8) else @truncate(@as(c_uint, @bitCast(limb)));
        e.unk6 = ctx.edge(i16, &.{ -1, 0, 1, 2, 3, 4, -32768 });
    }
    v.g.D_800F1BAC().* = @ptrCast(q);
    v.g.D_800F227E().* = n;

    const fmt = v.g.D_800691E4();
    fmt[0] = '%';
    fmt[1] = 'd';
    fmt[2] = 0;

    const value = ctx.pick(c_int, &.{ 0, 999, 1000, 1001, 0x7FFF, 0x8000 + 999, 0x8000 + 1000, -1, -2147483648, -2147483648 + 999, -2147483648 + 1000, 0x7FFF_FFFF });
    const flags = ctx.pick(c_int, &.{ 0, 1, 127, 128, 129, 255, 256 + 128 });
    return .{ actor, limb_index, value, flags };
}

pub fn classify(args: Args) usize {
    if (args[1] != -1 and limb_result < 0) return 0;
    if (anchor_result != 0) return 1;
    if (args[3] & 128 != 0) {
        if (screen_result != 0) return 2;
        if (@as(u16, @bitCast(screen.vx)) >= 321 or @as(u16, @bitCast(screen.vy)) >= 241) return 3;
    }
    if (position_result != 0) return 4;
    return 5;
}
