//! zig_diff spec: func_800981B8 -> pc.set_room_clip_rect.setRoomClipRect (D2: pc/src/room_render.c).
//! Inputs: four ints, each a short's value (edges included) or any int, which the
//! C truncates to the file statics clip_left, clip_top, clip_right and clip_bottom
//! (globalised, compared): all four differ, so a swapped pair shows.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setRoomClipRect));

pub const classes = [_][:0]const u8{ "fits", "wraps" };

fn one(ctx: *zd.Ctx, wide: bool) c_int {
    if (wide) return ctx.pick(c_int, &.{ 0x8000, -0x8001, 0x10140, 0x1ffff, -0x80000000, 0x7fffffff });
    return ctx.pick(c_int, &.{ 0, 320, 224, 0x7fff, -0x8000, -1 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const wide = ctx.chance(2);
    return .{ one(ctx, wide), one(ctx, wide), one(ctx, wide), one(ctx, wide) };
}

pub fn classify(args: Args) usize {
    inline for (args) |a| if (a < -0x8000 or a > 0x7fff) return 1;
    return 0;
}
