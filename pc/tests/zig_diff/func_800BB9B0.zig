//! zig_diff spec: func_800BB9B0 -> func_800BB9B0.func_800BB9B0 (4A0A8.c).
//! Inputs: the global tick speed (0, 1, 2, 3, negatives, the i32 extremes; 0 and 1
//! make the half-tick step zero, so a slot's unk2 can equal its unk3 after the step),
//! three D_800F4B30 slots with any u8 unk1/unk2, unk3 at the zero, sign and 0x80 edges
//! (unk2 equal to unk3 a third of the time), and edge i16 fields for the lerp terms.
//! The out table D_800EB9BC is either null or three contiguous 1016-byte blocks in the
//! arena. func_800BFEBC is a recording stub that returns any i32 (truncated to short by
//! the C, sign included).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB9B0));

pub const classes = [_][:0]const u8{ "no-out", "all-idle", "finishes", "running" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const tick = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, 0x7FFF_FFFF, -0x8000_0000 });
    v.g.vs_gametime_tickspeed().* = tick;

    const slots = v.g.D_800F4B30();
    for (0..3) |i| {
        const s = &slots[i];
        s.unk1 = ctx.int(u8);
        s.unk2 = ctx.int(u8);
        s.unk3 = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
        if (ctx.chance(3)) s.unk2 = s.unk3;
        s.unk4 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unk8 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unkA = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unkC = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unkE = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unk10 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
        s.unk12 = ctx.pick(i16, &.{ 0, 1, -1, 4096, -4096, 32767, -32768 });
    }

    if (ctx.chance(5)) {
        v.g.D_800EB9BC().* = null;
    } else {
        const bytes = ctx.allocBytes(3 * @sizeOf(v.D_800EB9BC_t), 16);
        const out: *v.D_800EB9BC_t = @ptrCast(@alignCast(bytes));
        v.g.D_800EB9BC().* = out;
    }

    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800EB9BC().* == null) return 0;
    const tick = v.g.vs_gametime_tickspeed().*;
    const slots = v.g.D_800F4B30();
    var running = false;
    for (0..3) |i| {
        const s = &slots[i];
        if (s.unk3 == 0) continue;
        const nu: u8 = @truncate(@as(u32, s.unk2) +% @as(u32, @bitCast(tick >> 1)));
        if (nu >= s.unk3) return 2;
        running = true;
    }
    return if (running) 3 else 1;
}

pub const stubs = struct {
    pub fn func_800BFEBC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) i32 {
        _ = a;
        _ = b;
        _ = c;
        return ctx.value(i32);
    }
};
