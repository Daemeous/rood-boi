//! zig_diff spec: func_800CDCBC -> func_800CDCBC.func_800CDCBC (5BF94.c).
//! Inputs: an arena trail whose config picks the Gouraud kind (bits 20..23
//! 6) or the textured one (bits 16..19 7 or not), its colour 0..7 (the
//! tables hold 8) and texture 0..15; head and tail brightnesses at their
//! edges. RotAverage4's stub writes the C's stack screen points and returns
//! a depth in, at or past the drawn range 17..2047. The packet cursor and
//! the OT (2048 words) are arena blocks.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CDCBC));

pub const classes = [_][:0]const u8{ "culled", "gouraud", "textured" };

var depth_ret: c_int = 0;

pub const stubs = struct {
    pub fn RotAverage4(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype, s0: anytype, s1: anytype, s2: anytype, s3: anytype, p: anytype, flag: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        s0.* = ctx.value(c_int);
        s1.* = ctx.value(c_int);
        s2.* = ctx.value(c_int);
        s3.* = ctx.value(c_int);
        p.* = ctx.value(c_int);
        flag.* = ctx.value(c_int);
        return depth_ret;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = ctx.alloc(v.effectTrailState);
    var value: u32 = ctx.int(u32);
    if (ctx.random.boolean()) value = (value & ~@as(u32, 0xF00000)) | 0x600000;
    if (ctx.chance(3)) value = (value & ~@as(u32, 0xF0000)) | 0x70000;
    value &= ~@as(u32, 0x80000); // colour 0..7
    t.config.value = @bitCast(value);
    depth_ret = ctx.pick(c_int, &.{ 16, 17, 18, 1000, 2047, 2048, -1 });
    if (depth_ret < 0 or depth_ret > 2047) depth_ret = ctx.edge(c_int, &.{ -1, 2048, 0x7FFFFFFF });
    const scratch = v.g.vs_scratch();
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(@sizeOf(v.TrailPacket) + 8, 16))));
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(2048 * 4, 16))));
    return .{ t, ctx.pick(c_int, &.{ 0, 1, 8, 16, 32, -1 }), ctx.pick(c_int, &.{ 0, 1, 8, 16, 32, -1 }) };
}

pub fn classify(args: Args) usize {
    if (depth_ret < 17 or depth_ret > 2047) return 0;
    return if ((args[0].?.config.value & 0xF00000) == 0x600000) 1 else 2;
}
