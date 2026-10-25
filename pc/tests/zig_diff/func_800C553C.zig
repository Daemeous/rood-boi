//! zig_diff spec: func_800C553C -> func_800C553C.func_800C553C (5BF94.c).
//! Inputs: the battle object with its element count unk2C (0, 1, 2, 6, 7,
//! 255 or any u8) and a 256-entry element array whose group bytes (unk9) are
//! one uniform group (every count equals the element count, so count == size
//! for sizes 1-6), scattered groups 0-3, or groups drawn from 15-255 (the
//! second loop's group >> 4 test, and the first loop's groups only reach
//! 0-15). The callee stub rewrites D_800F4CB0 and D_800F4CB1, the two globals
//! the C reads back, with CB1 equal to CB0 about a quarter of the time, so
//! the clamp `i < D_800F4CB0` sees both orders and both sides equal, and
//! `(16 - i) * 11 >> 1` sees negative values for i above 16.

const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C553C));

pub const classes = [_][:0]const u8{ "empty", "uniform-group", "scattered", "high-groups" };

var mode: u8 = 0;
var count_used: u8 = 0;

pub const stubs = struct {
    pub fn func_800C5360(ctx: *zd.Ctx, group: anytype) void {
        _ = group;
        const x = ctx.value(u8);
        v.g.D_800F4CB0().* = x;
        v.g.D_800F4CB1().* = if (ctx.value(u2) == 0) x else ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C56C0_t);
    const elems = ctx.alloc([256]v.func_800C5798_t);
    psx.put(&obj.unk40, &elems[0]);

    const n: u8 = ctx.pick(u8, &.{ 0, 1, 2, 6, 7, 255 });
    obj.unk2C = n;
    count_used = n;

    mode = ctx.range(u8, 0, 2);
    const uniform = ctx.range(u8, 0, 15);
    for (0..256) |i| {
        elems[i].unk9 = switch (mode) {
            0 => uniform,
            1 => ctx.range(u8, 0, 3),
            else => ctx.pick(u8, &.{ 15, 16, 17, 31, 128, 255 }),
        };
    }

    v.g.D_800EB9B8().* = obj;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (count_used == 0) return 0;
    return 1 + @as(usize, mode);
}
