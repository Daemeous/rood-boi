//! zig_diff spec: func_800CE174 -> func_800CE174.func_800CE174 (5BF94.c).
//! Inputs: a node whose trail is an arena state (its index any, its age
//! before, at and past the drawing (4) and the full length (9), its life
//! 0, 1, 2 or 255); the mode 1..4, 0 or 5; the config any. The heap
//! allocator returns an arena block; the libgte calls, the samplers, the
//! segment drawer are recording stubs; the spline's stub writes its output
//! (a point of the trail, an arena block).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CE174));

pub const classes = [_][:0]const u8{ "new", "young", "drawn", "life", "free", "other" };

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(size, 16));
    }
    pub fn vs_battle_splineInterpolate(ctx: *zd.Ctx, p0: anytype, p1: anytype, p2: anytype, p3: anytype, t: anytype, out: anytype) void {
        _ = p0;
        _ = p1;
        _ = p2;
        _ = p3;
        _ = t;
        const o: *v.SVECTOR = @ptrCast(@alignCast(out));
        o.vx = ctx.value(i16);
        o.vy = ctx.value(i16);
        o.vz = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const node = ctx.alloc(v.func_800D4910_t);
    const t = ctx.alloc(v.effectTrailState);
    node.unk8 = @ptrCast(t);
    t.age = ctx.edge(i32, &.{ 0, 2, 3, 4, 7, 8, 9, 100 });
    t.config.bytes[3] = ctx.edge(u8, &.{ 0, 1, 2, 255 });
    return .{ node, ctx.edge(c_uint, &.{ 1, 2, 2, 2, 3, 4, 0, 5 }), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const t: *v.effectTrailState = @ptrCast(@alignCast(args[0].*.unk8.?));
    return switch (args[1]) {
        1 => 0,
        2 => if (t.age >= 3) 2 else 1,
        3 => 3,
        4 => 4,
        else => 5,
    };
}
