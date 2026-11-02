//! zig_diff spec: func_800D5738 -> func_800D5738.func_800D5738 (5BF94.c).
//! Inputs: a command whose unk0 points into an arena array of 64 random
//! words, its index unk6 0..30 (so unk6 * 2 + 1 stays inside) or -1 (the
//! word before: the array starts 8 bytes into its block); func_800D12D8 is a
//! stub returning any int (narrowed into unkA).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5738));

pub const classes = [_][:0]const u8{ "first", "later", "before" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cmd = ctx.alloc(v.func_800D5780_t);
    const words: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(8 + 64 * 4, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(words + 2)));
    cmd.unk6 = ctx.pick(i16, &.{ 0, 1, 30, -1 });
    if (cmd.unk6 > 30 or cmd.unk6 < -1) cmd.unk6 = @rem(cmd.unk6, 31);
    if (cmd.unk6 < -1) cmd.unk6 = -1;
    return .{cmd};
}

pub fn classify(args: Args) usize {
    const i = args[0].?.unk6;
    if (i < 0) return 2;
    return if (i == 0) 0 else 1;
}
