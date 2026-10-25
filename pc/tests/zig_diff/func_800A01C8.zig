//! zig_diff spec: func_800A01C8 -> func_800A01C8.func_800A01C8 (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 (the only slots the C may read: an
//! out-of-range index would read globals the harness does not set, and a random
//! pointer there would be written through), with the slot null (a quarter of the
//! runs) or a fresh arena object, and three int arguments drawn from edges (0,
//! -1, 255, 256, INT_MAX, INT_MIN) or any value, so the u8 stores see truncation,
//! wrapping and the sign bit. The C has no callees, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A01C8));

pub const classes = [_][:0]const u8{ "null-slot", "object-slot-low", "object-slot-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.D_800F4538();
    const k = ctx.range(c_int, 0, 16);
    if (ctx.chance(4)) {
        slots[@intCast(k)] = null;
    } else {
        slots[@intCast(k)] = ctx.alloc(v.D_800F4538_t);
    }
    const edges = [_]c_int{ 0, -1, 255, 256, 0x7FFFFFFF, -2147483648 };
    return .{
        k,
        ctx.pick(c_int, &edges),
        ctx.pick(c_int, &edges),
        ctx.pick(c_int, &edges),
    };
}

pub fn classify(args: Args) usize {
    const slots = v.g.D_800F4538();
    const k: usize = @intCast(args[0]);
    if (slots[k] == null) return 0;
    return if (k < 8) 1 else 2;
}
