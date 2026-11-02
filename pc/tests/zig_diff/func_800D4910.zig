//! zig_diff spec: func_800D4910 -> func_800D4910.func_800D4910 (5BF94.c).
//! Inputs: a script whose effect list holds 0..3 arena nodes; each node's
//! renderer is a spec function that logs its call into the node (unk8)
//! and returns 0 (the node is freed: func_800CF70C, a stub) or 1, and it may
//! cut the list after it (the C read next before the call, so it goes on).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4910));

pub const classes = [_][:0]const u8{ "empty", "kept", "freed" };

var ret0: [3]c_int = .{ 0, 0, 0 };
var nodes: [3]?*v.func_800D4910_t = .{ null, null, null };
var freed = false;

fn renderer(node: [*c]c.func_800D4910_t, a: c.u_int, b: c_int) callconv(.c) c_int {
    const n: *v.func_800D4910_t = @ptrCast(@alignCast(node));
    var k: usize = 0;
    while (k < 3 and nodes[k] != n) k += 1;
    n.unk8 = @ptrFromInt(0x1000 + @as(usize, a) * 16 + @as(usize, @as(u32, @bitCast(b))) + k);
    if (k < 3 and ret0[k] == 2) n.next = null;
    return if (k < 3 and ret0[k] == 0) 0 else 1;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    const count = ctx.range(usize, 0, 3);
    freed = false;
    var next: ?*v.func_800D4910_t = null;
    var k: usize = 3;
    while (k > 0) {
        k -= 1;
        nodes[k] = null;
        if (k >= count) continue;
        const n = ctx.alloc(v.func_800D4910_t);
        n.next = next;
        n.renderer = @ptrCast(&renderer);
        ret0[k] = ctx.range(c_int, 0, 2);
        if (ret0[k] == 0) freed = true;
        nodes[k] = n;
        next = n;
    }
    psx.put(&s.unkD1C.unk34, next);
    return .{s};
}

pub fn classify(args: Args) usize {
    if (psx.get(args[0].?.unkD1C.unk34) == null) return 0;
    return if (freed) 2 else 1;
}
