//! zig_diff spec: func_800C2368 -> func_800C2368.func_800C2368 (58578.c).
//! Inputs: a radius of -3..40 (4..6 about a third of the time, so radius 5 and
//! the second call are common), a step of either sign (small or up to 4096),
//! and an index in -512..512. The vertex buffer is an arena block with room on
//! both sides of the global pointer, so a negative index still lands in memory
//! the harness owns. rcos and rsin return any i16, so the x*cos product and the
//! i16 stores truncate. func_800C2254 writes one scratchpad basis slot before it
//! returns, so the port must store basis before the call, and it returns any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C2368));

pub const classes = [_][:0]const u8{ "radius-negative", "radius-zero", "radius-1-to-4", "radius-5", "radius-over-5" };

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(i16));
    }

    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800C2254(ctx: *zd.Ctx, angle: anytype, index: anytype) c_int {
        _ = angle;
        _ = index;
        // The callee writes one basis slot of the scratchpad (the C never reads it back).
        const basis: *[9]i16 = @ptrFromInt(zd.scratchpad + 0x398);
        basis[ctx.value(u4) % 9] = ctx.value(i16);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // 16 KB block; the global points 8 KB in, so index -512 (about -4 KB) and
    // the 164 vertices written past the index stay inside the block.
    const block = ctx.allocBytes(16384, 16);
    const vtx: *v.D_800EB9B8_t = @ptrCast(@alignCast(block + 8192));
    v.g.D_800EB9B8().* = vtx;

    const radius: c_int = if (ctx.chance(3)) ctx.range(c_int, 4, 6) else ctx.range(c_int, -3, 40);
    const step: c_int = if (ctx.chance(4)) ctx.range(c_int, -2, 2) else ctx.range(c_int, -4096, 4096);
    const index: c_int = if (ctx.chance(4)) ctx.range(c_int, -2, 2) else ctx.range(c_int, -512, 512);
    return .{ step, radius, index };
}

pub fn classify(args: Args) usize {
    const radius = args[1];
    if (radius < 0) return 0;
    if (radius == 0) return 1;
    if (radius < 5) return 2;
    if (radius == 5) return 3;
    return 4;
}
