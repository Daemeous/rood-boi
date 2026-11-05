//! zig_diff spec: charge_skip_hold_vsyncs -> charge_skip.holdVsyncs (146C.c, charge_skip.inc).
//! Inputs: the caster 0 (Ashley: the "mine" setting) or another, the two hold settings at
//! their edges (0, negative, NaN, infinity, the 10 s clamp, 0.5 vsync rounding, whole vsyncs).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").holdVsyncs));

pub const classes = [_][:0]const u8{ "not-positive", "mine", "enemy", "clamped" };

fn seconds(ctx: *zd.Ctx) f32 {
    return switch (ctx.range(u8, 0, 9)) {
        0 => 0.0,
        1 => -1.5,
        2 => @bitCast(@as(u32, 0x7FC0_0000)),
        3 => @bitCast(@as(u32, 0x7F80_0000)),
        4 => 10.0,
        5 => 10.25,
        6 => 0.008,
        7 => 0.0084,
        else => @as(f32, @floatFromInt(ctx.range(i32, 1, 700))) / 60.0,
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.pc_qol_skip_charging_hold_mine().* = seconds(ctx);
    v.g.pc_qol_skip_charging_hold_enemy().* = seconds(ctx);
    return .{ctx.edge(c_int, &.{ 0, 0, 1, -1, 16, 255, std.math.minInt(c_int), std.math.maxInt(c_int) })};
}

pub fn classify(args: Args) usize {
    const s = if (args[0] == 0) v.g.pc_qol_skip_charging_hold_mine().* else v.g.pc_qol_skip_charging_hold_enemy().*;
    if (!(s > 0.0)) return 0;
    if (s > 10.0) return 3;
    return if (args[0] == 0) 1 else 2;
}
