//! zig_diff spec: func_800A07FC -> func_800A07FC.func_800A07FC (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (both ends included). Its slot is
//! null in a quarter of the runs, otherwise a fresh arena actor whose bytes are
//! random, so the bitfield read-modify-write and the copied struct are both
//! checked against leftover data. The source is null in 1 run of 3, otherwise
//! a random D_800F4538_unk58_2 from the arena. The C makes no external calls,
//! so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A07FC));

pub const classes = [_][:0]const u8{ "no-actor", "null-src", "copy" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const index = ctx.range(c_int, 0, 16);
    if (ctx.chance(4)) {
        table[@intCast(index)] = null;
    } else {
        table[@intCast(index)] = ctx.alloc(v.D_800F4538_t);
    }
    const src: ?*v.D_800F4538_unk58_2 = if (ctx.chance(3)) null else ctx.alloc(v.D_800F4538_unk58_2);
    return .{ index, src };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    if (table[@intCast(args[0])] == null) return 0;
    if (args[1] == null) return 1;
    return 2;
}
