//! zig_diff spec: func_800C7EBC -> func_800C7EBC.func_800C7EBC (5BF94.c).
//! Inputs: an arena image of 12 rows `stride` halfwords apart (12, 13 or
//! 320), the glyph any (its VRAM cell from % 21 and / 21), alpha 0, 0x8000,
//! 0x10000 or any (the blend is unsigned and wraps). StoreImage gets the
//! C's stack RECT and buffer (tags): its stub logs the RECT and fills the
//! buffer with 4-bit pixels (zeros among them: those keep the image).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C7EBC));

pub const classes = [_][:0]const u8{ "blend" };

var rect_log: ?*[4]i16 = null;

pub const stubs = struct {
    pub fn StoreImage(ctx: *zd.Ctx, rect: anytype, pixels: anytype) c_int {
        const r: *v.RECT = @ptrCast(@alignCast(rect));
        rect_log.?.* = .{ r.x, r.y, r.w, r.h };
        const p: [*]u32 = @ptrCast(@alignCast(pixels));
        for (0..18) |i| {
            var word = ctx.value(u32);
            if (i % 3 == 0) word &= 0x0F0F0F0F;
            p[i] = word;
        }
        return 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    rect_log = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    const stride = ctx.edge(c_int, &.{ 12, 13, 320 });
    const image: [*c]u16 = @ptrCast(@alignCast(ctx.allocBytes(@intCast(12 * stride * 2), 16)));
    return .{ image, ctx.int(c_uint), stride, ctx.pick(c_uint, &.{ 0, 0x8000, 0x10000, 1 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
