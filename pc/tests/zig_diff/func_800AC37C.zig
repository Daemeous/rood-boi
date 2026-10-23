//! zig_diff spec: func_800AC37C -> func_800AC37C.func_800AC37C (40564.c).
//! Inputs: an arg0 in the table's 0..16 range and a selector that is one of
//! the switch's cases (0, 1, 2), the default (3, -1, the sign-bit and maximum
//! values), or a bitmask-like edge. The table D_800F4538 gets a chain of
//! 1 to 4 arena objects: each one's unk17FD names the next table index (2..16,
//! distinct, so the recursion ends and never loops), and the last one's
//! unk17FD is 0 or 1, so `> 1` fails there. Reads above 16 would index past
//! the table into memory the harness does not own, so they are not generated.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC37C));

pub const classes = [_][:0]const u8{ "other-selector", "leaf", "recursive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const depth = ctx.range(usize, 0, 3);
    var idx: [4]usize = undefined;
    idx[0] = ctx.range(usize, 0, 16);
    var n: usize = 1;
    while (n <= depth) {
        const k = ctx.range(usize, 2, 16);
        var used = false;
        for (idx[0..n]) |x| {
            if (x == k) used = true;
        }
        if (!used) {
            idx[n] = k;
            n += 1;
        }
    }
    var j: usize = 0;
    while (j <= depth) : (j += 1) {
        const obj = ctx.alloc(v.D_800F4538_t);
        table[idx[j]] = obj;
        if (j < depth) {
            obj.unk17FD = @as(u8, @intCast(idx[j + 1]));
        } else {
            obj.unk17FD = ctx.edge(u8, &.{ 0, 1 });
        }
    }
    const sel = ctx.edge(c_int, &.{ 0, 1, 2, 3, -1, 0x7FFF_FFFF, -2147483648 });
    return .{ @as(c_int, @intCast(idx[0])), sel };
}

pub fn classify(args: Args) usize {
    if (args[1] < 0 or args[1] > 2) return 0;
    const table = v.g.D_800F4538();
    const first = table[@as(usize, @intCast(args[0]))].?;
    return if (first.unk17FD > 1) 2 else 1;
}
