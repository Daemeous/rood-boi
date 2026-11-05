//! zig_diff spec: func_800C0B50 -> pc.draw_menu_target_shape.drawMenuTargetShape (D2: pc/src/battle_gte.c).
//! Inputs: the state block (the global D_800EB9B8 holds its address: angles at
//! +0x24, bias at +0x20, point count (0..5, usually 1..3) at +0x32, points at +0x48
//! with a pad half-word that is zero a third of the time) and a shape block of 16
//! random bytes (its two divisor bytes zero or small, so the zero divisor is
//! reached), and a colour. The libgte helpers are recording stubs; rcos, MFC2 and
//! the GTE are drawn, so the projected points, segment depths and colours are
//! what the calls to func_800C0990 show, and the scratchpad is compared whole.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_gte");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawMenuTargetShape));

pub const classes = [_][:0]const u8{ "no-points", "points", "zero-divisor" };

pub const stubs = struct {
    /// The projected depth (register 19) is zero half the time, so the segment's sum can be zero.
    pub fn MFC2(ctx: *zd.Ctx, reg: c_int) c_uint {
        const value = ctx.value(c_uint);
        return if (reg == 19 and value & 1 == 0) 0 else value;
    }
};

var count_now: u16 = 0;
var divisor_zero = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const buffer = ctx.allocBytes(0x48 + 5 * 8 + 16, 16);
    for (buffer[0 .. 0x48 + 5 * 8]) |*byte| byte.* = ctx.int(u8);
    count_now = if (ctx.chance(6)) 0 else ctx.range(u16, 1, if (ctx.chance(4)) 5 else 3);
    @as(*align(1) u16, @ptrCast(buffer + 0x32)).* = count_now;
    for (0..count_now) |i| {
        if (ctx.chance(3)) @as(*align(1) i16, @ptrCast(buffer + 0x48 + i * 8 + 6)).* = 0;
    }
    if (ctx.chance(3)) @as(*align(1) i16, @ptrCast(buffer + 0x20)).* = 0; // a zero bias
    v.g.D_800EB9B8().* = @intCast(@intFromPtr(buffer));
    const shape = ctx.allocBytes(16, 16);
    for (shape[0..16]) |*byte| byte.* = ctx.int(u8);
    divisor_zero = ctx.chance(4);
    shape[4] = if (divisor_zero) 0 else ctx.pick(u8, &.{ 1, 2, 0x20, 0xff });
    shape[6] = if (divisor_zero) 0 else ctx.pick(u8, &.{ 0, 1, 3, 0x40, 0xff });
    if (!divisor_zero and shape[4] == 0 and shape[6] == 0) shape[6] = 1;
    return .{ shape, ctx.pick(c_uint, &.{ 0, 0xffffff, 0x80000000, 0xffffffff }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (count_now == 0) return 0;
    return if (divisor_zero) 2 else 1;
}
