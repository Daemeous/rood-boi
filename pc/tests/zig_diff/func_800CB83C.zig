//! zig_diff spec: func_800CB83C -> func_800CB83C.func_800CB83C (5BF94.c).
//! Inputs: the eight text boxes with random bytes, then: state 0 (skipped),
//! opening (1.. past its duration) or closing (negative, past -duration),
//! the hidden flag (bit 31) set or not, the flags word 0 now and then, the open curve (flags bits 4..5)
//! any, the duration 1..255 (never 0, nor 2 with curve 2 at frame 0: both
//! divide by zero in the C), the kind 0..7 and past it; the tick speed 0..8;
//! the page D_800EB9CE 0..2 or past it; the timer shown or not. Every
//! callee is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CB83C));

pub const classes = [_][:0]const u8{"boxes"};

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..8) |i| {
        const box = &v.g.textBoxes()[i];
        const duration = ctx.pick(u8, &.{ 1, 2, 3, 4, 10, 255 }) | @intFromBool(ctx.chance(2));
        box.unk22 = duration;
        box.state = switch (ctx.range(u8, 0, 5)) {
            0 => 0,
            1 => 1,
            2 => @as(i16, duration),
            3 => @as(i16, duration) + 1,
            4 => -1,
            else => -@as(i16, duration),
        };
        const flags: *u32 = @ptrCast(@alignCast(&box.unk0));
        if (ctx.chance(5)) flags.* |= 0x80000000 else flags.* &= 0x7FFFFFFF;
        if (ctx.chance(8)) flags.* = 0;
        if ((flags.* >> 4) & 3 == 2 and duration <= 2) box.unk22 = 3;
        if (box.unk22 == 0) box.unk22 = 1;
        if (flags.* != 0) psx.setBits(&box.unk0, "unk0_0", ctx.range(u4, 0, 9));
    }
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 0, 1, 2, 4, 8 });
    v.g.D_800EB9CE().* = ctx.pick(u8, &.{ 0, 1, 2 });
    v.g.vs_battle_lowerScreenUiState().* = ctx.pick(u8, &.{ 0, 1, 1 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
