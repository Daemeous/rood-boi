//! zig_diff spec: vs_battle_renderStatBar -> stat_bar_gauge.renderStatBar (5BF94.c).
//! Inputs: the colour index 0..19 (D_800EBC04's 20 words), w 0..64 (0 and
//! 64 too) or past it (the blend goes negative: unsigned in the C); the
//! packet cursor in the scratchpad (its address has a high byte the link
//! drops) or the arena; `before` an arena word; xy any.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderStatBar));

pub const classes = [_][:0]const u8{ "empty", "partial", "full", "past" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch = v.g.vs_scratch();
    const packet: *anyopaque = if (ctx.random.boolean()) @ptrFromInt(0x1F800100 + 8 * ctx.range(usize, 0, 15)) else @ptrCast(ctx.allocBytes(56, 16));
    psx.put(&scratch.unk0, packet);
    const before: *c_uint = @ptrCast(@alignCast(ctx.allocBytes(4, 16)));
    const w: c_int = switch (ctx.range(u8, 0, 3)) {
        0 => 0,
        1 => ctx.range(c_int, 1, 63),
        2 => 64,
        else => ctx.pick(c_int, &.{ 65, 200, -1, 0x7FFF_FFFF }),
    };
    return .{ ctx.range(c_int, 0, 19), w, before, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const w = args[1];
    if (w == 0) return 0;
    if (w < 64 and w > 0) return 1;
    return if (w == 64) 2 else 3;
}
