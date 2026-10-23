//! zig_diff spec: func_800941FC -> func_800941FC.func_800941FC (2842C.c).
//! Inputs: a label list of 0 to 6 entries (count -1 and 0 included) in an
//! arena array of 8, with every type 0-255 (types 1-2 run the position path,
//! 3 the RotTransPers path, 4-7 the switch, 8 and up fall through). Timers sit
//! on the 10/31/46/61/91 thresholds, unk6 counters on 0, and the frame counter
//! on 20 (the countdown pass runs when it is over). The tick speed covers odd,
//! negative and extreme values, so the halving and the u8/i16 wraps are hit.
//! Countdown inputs make every label a type-6 entry so the inner loop runs.
//! Stubs draw screen, visible, position and return values from edge lists, so
//! the clamps, the remove paths and the zero tests each go both ways.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800941FC));

const label_slots = 8;

pub const classes = [_][:0]const u8{
    "no-labels",
    "type-0",
    "type-1-2",
    "type-3",
    "type-4-5",
    "type-6",
    "type-7",
    "other-types",
};

fn labelsOf() [*]v.D_800F1BAC_t {
    const p: *anyopaque = v.g.D_800F1BAC().*.?;
    return @ptrCast(@alignCast(p));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const labels: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.D_800F1BAC_t) * label_slots, 16)));
    const countdown = ctx.chance(4);

    v.g.D_800F227E().* = ctx.edge(i16, &.{ -1, 0, 1, 2, 3, 4, 5, 6 });
    v.g.D_800F227C().* = ctx.int(i16);
    v.g.D_800F2272().* = ctx.edge(i16, &.{ -1, 0, 0, 1, 2 });
    v.g.D_800F1BA4().* = ctx.edge(u16, &.{ 0, 0, 1, 2, 0x8000, 0xFFFF });
    v.g.D_800F2278().* = if (countdown) 40 else ctx.edge(i16, &.{ 0, 19, 20, 20, 21, -1, 30, 32767, -32768 });
    v.g.D_800F227A().* = ctx.edge(i16, &.{ -1, 0, 0, 1 });
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 0, 1, 2, 3, 4, -1, -2, -3, 7, 0x7FFFFFFF, -0x80000000 });
    v.g.D_800F1BAC().* = @ptrCast(labels);

    for (0..label_slots) |k| {
        const l = &labels[k];
        l.unk0 = ctx.int(i16);
        l.unk2 = ctx.int(i16);
        l.unk4 = ctx.int(i16);
        l.unk6 = if (countdown) ctx.edge(i16, &.{ 0, 0, 1, -1 }) else ctx.edge(i16, &.{ -2, -1, 0, 0, 1, 2 });
        l.unk8.u8[0] = if (countdown) 6 else ctx.edge(u8, &.{ 0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 200, 255 });
        l.unk8.u8[1] = ctx.edge(u8, &.{ 0, 0, 1, 5, 255 });
        l.unkA = ctx.int(u8);
        l.unkB = ctx.edge(u8, &.{ 0, 4, 8, 9, 10, 20, 29, 30, 31, 44, 45, 46, 59, 60, 61, 89, 90, 91, 120, 255 });
        l.unkC = ctx.int(u16);
        l.unkE = ctx.edge(u8, &.{ 0, 1, 2, 2, 0x40, 0x7F, 0x80, 0x81, 0xFF });
        l.displayTextLen = ctx.int(u8);
        l.unk10 = ctx.int(i16);
        l.unk12 = ctx.int(i16);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F227E().* <= 0) return 0;
    const labels = labelsOf();
    return switch (labels[0].unk8.u8[0]) {
        0 => 1,
        1, 2 => 2,
        3 => 3,
        4, 5 => 4,
        6 => 5,
        7 => 6,
        else => 7,
    };
}

fn touchTimer(ctx: *zd.Ctx, label: anytype) void {
    const l: *align(1) v.D_800F1BAC_t = @ptrCast(label);
    if (ctx.value(u8) < 128) l.unkB = ctx.value(u8);
}

pub const stubs = struct {
    pub fn func_800970BC(ctx: *zd.Ctx) void {
        if (ctx.value(u8) < 128) v.g.D_800F2278().* = ctx.value(i16);
    }

    pub fn func_800A190C(ctx: *zd.Ctx, kind: anytype, arg: anytype, pos: anytype, zero: anytype) c_int {
        _ = kind;
        _ = arg;
        _ = zero;
        const ys = [_]i16{ 0, 0, 1, -1, 32767, -32768 };
        const sv: *align(1) v.SVECTOR = @ptrCast(pos);
        sv.vx = ctx.value(i16);
        sv.vy = ys[ctx.value(u8) % ys.len];
        sv.vz = ctx.value(i16);
        return if (ctx.value(u8) < 64) -1 else 0;
    }

    pub fn RotTransPers(ctx: *zd.Ctx, vec: anytype, sxy: anytype, p: anytype, flag: anytype) c_int {
        _ = vec;
        _ = p;
        _ = flag;
        const xs = [_]i16{ 31, 32, 33, 287, 288, 289, -5, 0 };
        const ys = [_]i16{ 39, 40, 41, 199, 200, 201, 0, -1 };
        const s: *align(1) [2]i16 = @ptrCast(sxy);
        s[0] = xs[ctx.value(u8) % xs.len];
        s[1] = ys[ctx.value(u8) % ys.len];
        return switch (ctx.value(u8) & 3) {
            0 => 0,
            1 => -1,
            else => 1,
        };
    }

    pub fn func_80094844(ctx: *zd.Ctx, screen: anytype, label: anytype) void {
        _ = screen;
        const l: *align(1) v.D_800F1BAC_t = @ptrCast(label);
        if (ctx.value(u8) < 128) l.unkE = ctx.value(u8);
    }

    pub fn func_80094AF8(ctx: *zd.Ctx, label: anytype) void {
        const l: *align(1) v.D_800F1BAC_t = @ptrCast(label);
        if (ctx.value(u8) < 128) l.unk6 = ctx.value(i16);
    }

    pub fn func_800958A4(ctx: *zd.Ctx, x: anytype, y: anytype, label: anytype) void {
        _ = x;
        _ = y;
        touchTimer(ctx, label);
    }

    pub fn func_80095A4C(ctx: *zd.Ctx, x: anytype, y: anytype, label: anytype) void {
        _ = x;
        _ = y;
        touchTimer(ctx, label);
    }

    pub fn func_8009695C(ctx: *zd.Ctx, x: anytype, y: anytype, label: anytype) void {
        _ = x;
        _ = y;
        touchTimer(ctx, label);
    }
};
