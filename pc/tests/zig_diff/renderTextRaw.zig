//! zig_diff spec: vs_battle_renderTextRaw -> render_text_raw.renderTextRaw (5BF94.c).
//! The C has no branch of its own: it forwards text and xy and nextPrim to
//! vs_battle_renderTextRawColor with the constant colour 0x808080 (128, 128, 128
//! packed as RGB, code 0). The inputs cover a null and a valid text pointer,
//! edge values of xy (0, -1, INT_MAX, INT_MIN, a high byte set), and a null
//! and a valid nextPrim. The callee is a recording stub: it changes no state
//! and returns nothing, and the trace compares its arguments.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderTextRaw));

pub const classes = [_][:0]const u8{ "text", "null-text" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    const text: [*c]const u8 = if (ctx.chance(8)) null else @as([*c]const u8, buf);
    const xy = ctx.pick(c_int, &.{ 0, -1, std.math.maxInt(c_int), std.math.minInt(c_int), 0x00FF_0000 });
    const next: ?*anyopaque = if (ctx.chance(4)) null else @as(?*anyopaque, @ptrCast(ctx.allocBytes(8, 8)));
    return .{ text, xy, next };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 1 else 0;
}
