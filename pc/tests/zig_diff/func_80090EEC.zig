//! zig_diff spec: func_80090EEC -> func_80090EEC.func_80090EEC (2842C.c).
//! Inputs: the room is in the arena or missing (sectionB null); the gates
//! D_800F1DCA (0..6 and 255), D_800F1BA4 and D_800F1DCB are zero or not; the
//! D_800F1CDC word is picked from edges that make each mask test zero or not;
//! the three room masks are 0..3 (the masks[] index); the clamp bounds are
//! i16 edges, so inverted bounds occur; D_800F1D98[0..5] and D_800F1DC4 come
//! from i16 edges. rand() returns any value, so `(rand() & 7) < 3` goes both
//! ways and sees equal sides often. The stubs write the globals the C reads
//! after a call (D_800F1DCA, D_800F1D98[0], D_800F1DC4).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80090EEC));

pub const classes = [_][:0]const u8{
    "no-room",
    "idle",
    "block-skipped",
    "case-1",
    "case-2",
    "case-3",
    "case-4",
    "other-case",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const g = v.g;
    const room = ctx.alloc(v._mpdRoomSectionB);
    room.unk3A = ctx.range(i16, 0, 3);
    room.unk40 = ctx.range(i16, 0, 3);
    room.unk46 = ctx.range(i16, 0, 3);
    room.unk36 = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });
    room.unk38 = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });
    room.unk3C = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });
    room.unk3E = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });
    room.unk42 = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });
    room.unk44 = ctx.pick(i16, &.{ 0, 1, -1, 100, -100, 4095, 4096, 32767, -32768 });

    const field: ?*v._mpdRoomSectionB = if (ctx.chance(10)) null else room;
    psx.put(&g.vs_battle_roomData().sectionB, field);

    g.D_800F1CDC().* = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x7FFF, 0xFFE2, 0x1E, 0x3F, 0x40 });
    g.D_800F1BA4().* = if (ctx.chance(5)) ctx.pick(u16, &.{ 1, 0x8000, 0xFFFF }) else 0;
    g.D_800F1DCB().* = if (ctx.chance(6)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    g.D_800F1DCA().* = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 255 });
    g.D_800F1DC4().* = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    g.D_800F1DA8().* = ctx.pick(i16, &.{ 0, 1, -1, 16, 32767, -32768 });
    const d98 = g.D_800F1D98();
    for (0..6) |i| {
        d98[i] = ctx.pick(i16, &.{ 0, 1, -1, 2047, 4095, 4096, 32767, -32768 });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const g = v.g;
    if (psx.get(g.vs_battle_roomData().sectionB) == null) return 0;
    const dca = g.D_800F1DCA().*;
    if (dca == 0) return 1;
    if (g.D_800F1BA4().* != 0 or g.D_800F1DCB().* == 0) return 2;
    return switch (dca) {
        1 => 3,
        2 => 4,
        3 => 5,
        4 => 6,
        else => 7,
    };
}

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        return @intCast(ctx.value(u31));
    }

    pub fn vs_battle_clamp(ctx: *zd.Ctx, x: anytype, lo: anytype, hi: anytype) c_int {
        _ = ctx;
        const xi: i64 = x;
        const l: i64 = lo;
        const h: i64 = hi;
        const r = if (xi < l) l else if (xi > h) h else xi;
        return @intCast(r);
    }

    pub fn func_8008F440(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_8008FAC8(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_8008FDC4(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_80090434(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_80090B28(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F1DCA().* = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g.D_800F1D98()[0] = ctx.value(i16);
    }

    pub fn func_8009121C(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F1DC4().* = ctx.value(i16);
    }
};
