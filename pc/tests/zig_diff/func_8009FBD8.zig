//! zig_diff spec: func_8009FBD8 -> func_8009FBD8.func_8009FBD8 (30DB0.c).
//! Inputs: an index into the 17-entry battle actor table D_800F4538 (0..16,
//! the whole table; each entry is randomly null or an arena D_800F4538_t, so
//! the null return and the dereferenced store both run), and a selector
//! argument that is -1 (the 0xD store), another negative (-2, INT_MIN), 0,
//! 13, 255, 256 (stored truncated to a byte) or any value. The function has
//! no callees, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FBD8));

pub const classes = [_][:0]const u8{ "null-entry", "minus-one", "store-value" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    // Every entry is set: null about a quarter of the time, otherwise the arena object.
    for (table) |*e| {
        e.* = if (ctx.chance(4)) null else obj;
    }
    const idx = ctx.range(c_int, 0, 16);
    const arg1 = ctx.pick(c_int, &.{ -1, -1, 0, 13, 255, 256, -2, std.math.minInt(c_int) });
    return .{ idx, arg1 };
}

pub fn classify(args: Args) usize {
    const e = v.g.D_800F4538()[@intCast(args[0])];
    if (e == null) return 0;
    return if (args[1] == -1) 1 else 2;
}
