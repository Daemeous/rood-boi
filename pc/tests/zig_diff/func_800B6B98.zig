//! zig_diff spec: func_800B6B98 -> func_800B6B98.func_800B6B98 (4A0A8.c).
//! Inputs: an origin whose vx and vy sit near the screen clamps (within a few
//! units of 4, 216 - height, 320 - width or random interior values, so the
//! C's comparisons see equal sides often), a script whose width and height
//! bytes include the wrapping and edge values, a result placement, and any
//! direction byte. One time in eight a coordinate is any i16 instead, so the
//! C's short sums and clamp deltas wrap and dx * dx + dy * dy overflows int.
//! func_800CCE10 returns a random textOffset packed into the u32 the C
//! receives, whose x and y are small signed offsets (any 12-bit value one
//! time in eight); vs_gte_rsqrt returns any i32. One time in sixteen the
//! origin is (-32764, -32764) with a zero offset: both clamp shifts are then
//! -32768 and the C's dx * dx + dy * dy wraps to INT_MIN.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6B98));

pub const classes = [_][:0]const u8{ "near-left", "near-right", "near-top", "near-bottom", "interior" };

/// Set by prepare: func_800CCE10's x and y are 0 (the wrap case).
var zero_offset = false;

pub const stubs = struct {
    pub fn func_800CCE10(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) u32 {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        var bytes: [4]u8 = undefined;
        for (&bytes) |*byte| byte.* = ctx.value(u8);
        const t: *v.textOffset = @ptrCast(&bytes);
        const wide = ctx.value(u8) < 32;
        if (zero_offset) {
            psx.setBits(t, "x", 0);
            psx.setBits(t, "y", 0);
            return std.mem.readInt(u32, &bytes, .little);
        }
        psx.setBits(t, "x", if (wide) ctx.value(i12) else @as(i12, @intCast(@as(i32, ctx.value(u8) % 7) - 3)));
        psx.setBits(t, "y", if (wide) ctx.value(i12) else @as(i12, @intCast(@as(i32, ctx.value(u8) % 7) - 3)));
        return std.mem.readInt(u32, &bytes, .little);
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) i32 {
        _ = x;
        return ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const origin = ctx.alloc(v.SVECTOR);
    const script = ctx.allocBytes(8, 1);
    const result = ctx.alloc(v.textPlacement);

    script[5] = ctx.int(u8);
    script[6] = ctx.pick(u8, &.{ 0, 1, 2, 24, 25, 26, 27, 255 });
    script[7] = ctx.pick(u8, &.{ 0, 1, 2, 15, 16, 17, 255 });
    const w: i32 = @as(i32, script[6]) * 12 + 8;
    const h: i32 = @as(i32, script[7]) * 13 + 4;

    const xb: i32 = switch (ctx.range(u8, 0, 4)) {
        0 => 4,
        1 => 320 - w,
        2 => 0,
        else => ctx.range(i32, 9, 300),
    };
    const yb: i32 = switch (ctx.range(u8, 0, 4)) {
        0 => 4,
        1 => 216 - h,
        2 => 0,
        else => ctx.range(i32, 9, 150),
    };
    origin.vx = @truncate(xb + ctx.range(i32, -4, 4));
    origin.vy = @truncate(yb + ctx.range(i32, -4, 4));
    if (ctx.chance(8)) origin.vx = ctx.pick(i16, &.{ 32767, -32768, 32765 });
    if (ctx.chance(8)) origin.vy = ctx.pick(i16, &.{ 32767, -32768, 32765 });
    zero_offset = ctx.chance(16);
    if (zero_offset) {
        origin.vx = -32764;
        origin.vy = -32764;
    }
    origin.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    return .{ origin, script, result, ctx.int(u8) };
}

pub fn classify(args: Args) usize {
    const o = args[0].?;
    const s = args[1];
    const w: i32 = @as(i32, s[6]) * 12 + 8;
    const h: i32 = @as(i32, s[7]) * 13 + 4;
    const x: i32 = o.vx;
    const y: i32 = o.vy;
    if (x <= 8) return 0;
    if (x + w >= 312) return 1;
    if (y <= 8) return 2;
    if (y + h >= 208) return 3;
    return 4;
}
