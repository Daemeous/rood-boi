//! zig_diff spec: func_800A09D8 -> func_800A09D8.func_800A09D8 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (every entry in range, the table
//! itself is the only thing read), with the indexed entry either NULL (the
//! C's guard skips the store) or a random D_800F4538_t in the arena (the
//! store runs). The value is drawn from edges (0, 1, 2, -1, 3, INT_MAX, -2),
//! so the one-bit field sees both bit-0 set and clear, and even nonzero
//! values that store 0. The other table entries point at the arena object
//! and are never read. The object's bytes are random, so the bits beside
//! unk5AC_24 must survive the store. No calls are made.

const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A09D8));

pub const classes = [_][:0]const u8{ "null-entry", "live-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    for (table) |*slot| slot.* = obj;
    const idx = ctx.range(c_int, 0, 16);
    if (ctx.chance(3)) table[@intCast(idx)] = null;
    const val = ctx.pick(c_int, &.{ 0, 1, 2, -1, 3, 0x7FFF_FFFF, -2 });
    return .{ idx, val };
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F4538()[@intCast(args[0])] == null) 0 else 1;
}
