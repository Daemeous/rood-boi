//! zig_diff spec: func_800CE8F4 -> func_800CE8F4.func_800CE8F4 (5BF94.c).
//! Inputs: D_800F53B8's list of 1..4 arena scripts (never empty: the C
//! reads through it), the script freed first, later, last or not on it;
//! its effect list 0..3 nodes whose renderer is a spec function logging its
//! call into the node. func_800D2904, func_800CF70C and vs_main_freeHeapR
//! are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CE8F4));

pub const classes = [_][:0]const u8{ "head", "inside", "absent" };

var position: usize = 0;
var count: usize = 1;

fn renderer(node: [*c]c.func_800D4910_t, a: c.u_int, b: c_int) callconv(.c) c_int {
    const n: *v.func_800D4910_t = @ptrCast(@alignCast(node));
    n.unk8 = @ptrFromInt(0x1000 + @as(usize, a) * 16 + @as(usize, @as(u32, @bitCast(b))));
    n.next = null; // the C read next before the call: it goes on
    return 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const target = ctx.alloc(v.D_800F53B8_t);
    count = ctx.range(usize, 1, 4);
    position = ctx.range(usize, 0, count); // == count: not on the list
    var head: ?*v.D_800F53B8_t = null;
    var k: usize = count;
    while (k > 0) {
        k -= 1;
        const s = if (k == position) target else ctx.alloc(v.D_800F53B8_t);
        psx.put(&s.next, head);
        head = s;
    }
    if (position == count) psx.put(&target.next, @as(?*v.D_800F53B8_t, ctx.alloc(v.D_800F53B8_t)));
    v.g.D_800F53B8().* = head;
    var list: ?*v.func_800D4910_t = null;
    for (0..ctx.range(usize, 0, 3)) |_| {
        const n = ctx.alloc(v.func_800D4910_t);
        n.next = list;
        n.renderer = @ptrCast(&renderer);
        list = n;
    }
    psx.put(&target.unkD1C.unk34, list);
    return .{target};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (position == 0) return 0;
    return if (position < count) 1 else 2;
}
