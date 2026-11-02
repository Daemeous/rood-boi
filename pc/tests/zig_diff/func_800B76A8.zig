//! zig_diff spec: func_800B76A8 -> func_800B76A8.func_800B76A8 (4A0A8.c).
//! Inputs: a random 8-byte request block in the arena. arg0[5] selects the
//! D_800E9B58 entry (arg0[5] >> 6, always 0..3, inside the 4-entry table) and
//! the low six bits (arg0[5] & 0x3F); arg0[3] and arg0[4] are edge-biased
//! (0, 128, 255) so the u8 sum with the table entry wraps. vs_battle_getShort
//! returns any c_short, negatives included. The other two callees are left to
//! the harness defaults: the C never reads back anything they could change.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B76A8));

pub const classes = [_][:0]const u8{ "index-0", "index-1", "index-2", "index-3" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // Bytes 0..7 of the arena block: the C reads 1..5 only.
    const req: [*]u8 = ctx.allocBytes(8, 1);
    req[3] = ctx.pick(u8, &.{ 0, 128, 255 });
    req[4] = ctx.pick(u8, &.{ 0, 1, 255 });
    req[5] = ctx.int(u8);
    return .{req};
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][5] >> 6);
}
