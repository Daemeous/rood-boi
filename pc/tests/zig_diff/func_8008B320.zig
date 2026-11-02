//! zig_diff spec: func_8008B320 -> func_8008B320.func_8008B320 (146C.c).
//! Inputs: D_800F1BA8 is an arena block of 0x380 colours (the stubs hash
//! what their pointer arguments point at; func_80099960 copies 0xE0 bytes
//! from its argument in the game).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008B320));

pub const classes = [_][:0]const u8{"any"};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F1BA8().* = @ptrCast(@alignCast(ctx.allocBytes(0x380 * 2, 16)));
    return .{};
}

pub fn classify(_: Args) usize {
    return 0;
}
