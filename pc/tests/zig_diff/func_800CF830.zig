//! zig_diff spec: func_800CF830 -> func_800CF830.func_800CF830 (5BF94.c).
//! Inputs: D_800F54B0's list of 0..3 arena scripts, each with 0..3 effect
//! nodes whose renderer is func_800CE174 (the trail: a recording stub, so
//! its calls are compared), a spec function that must not be called, or
//! null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CF830));

pub const classes = [_][:0]const u8{ "no-scripts", "no-trail", "trail" };

var trail = false;

fn other(node: [*c]c.func_800D4910_t, a: c.u_int, b: c_int) callconv(.c) c_int {
    _ = a;
    _ = b;
    const n: *v.func_800D4910_t = @ptrCast(@alignCast(node));
    n.unk8 = @ptrFromInt(0xBAD);
    return 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    trail = false;
    var head: ?*v.D_800F53B8_t = null;
    for (0..ctx.range(usize, 0, 3)) |_| {
        const s = ctx.alloc(v.D_800F53B8_t);
        psx.put(&s.next, head);
        var list: ?*v.func_800D4910_t = null;
        for (0..ctx.range(usize, 0, 3)) |_| {
            const n = ctx.alloc(v.func_800D4910_t);
            n.next = list;
            n.renderer = switch (ctx.range(u8, 0, 2)) {
                0 => @ptrCast(&c.func_800CE174),
                1 => @ptrCast(&other),
                else => null,
            };
            if (n.renderer == @as(?*const anyopaque, @ptrCast(&c.func_800CE174))) trail = true;
            list = n;
        }
        psx.put(&s.unkD1C.unk34, list);
        head = s;
    }
    v.g.D_800F54B0().* = head;
    return .{ ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F54B0().* == null) return 0;
    return if (trail) 2 else 1;
}
