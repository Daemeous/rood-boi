//! zig_diff spec: func_800B7EF0 -> func_800B7EF0.func_800B7EF0 (4A0A8.c).
//! Inputs: a 6-byte block at arg0 whose byte 5 is 0xFF half the time (the
//! "no override" case) and an arbitrary byte otherwise, with arg1 random (the
//! C never reads it). The callee stubs return table indices below 24 (with
//! high bits set sometimes, which the C masks off) so the D_800F4B70 store
//! stays inside its table. func_800A1108 fills the out-parameter it is given;
//! func_800A9CDC may change a D_800F4B70 entry and the out-parameter bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7EF0));

pub const classes = [_][:0]const u8{ "ff-byte", "other-byte" };

pub const stubs = struct {
    /// Returns a table index below 24; sometimes with high bits set, which the
    /// C masks away with `& 0xFFFF`.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const k: u32 = ctx.value(u32) % 24;
        if (ctx.value(u8) < 32) return @bitCast(@as(u32, 0x10000) | k);
        if (ctx.value(u8) < 16) return @bitCast(@as(u32, 0xFFFF0000) | k);
        return @bitCast(k);
    }

    /// Fills the 16-byte out-parameter (the C's sp10) with random words.
    pub fn func_800A1108(ctx: *zd.Ctx, a: anytype, out: anytype) c_int {
        _ = a;
        if (out) |o| {
            const w: [*]u8 = @ptrCast(o);
            for (0..4) |i| {
                const word = ctx.value(u32);
                @memcpy(w[i * 4 ..][0..4], std.mem.asBytes(&word));
            }
        }
        return ctx.value(c_int);
    }

    /// May change one entry of D_800F4B70 and one byte of the fields it gets.
    pub fn func_800A9CDC(ctx: *zd.Ctx, idx: anytype, fields: anytype, flag: anytype) void {
        _ = idx;
        _ = flag;
        const k: usize = ctx.value(u8) % 24;
        if (ctx.value(u8) < 128) v.g.D_800F4B70()[k] = ctx.value(u8);
        const w: [*]u8 = @ptrCast(fields);
        w[ctx.value(u8) % 4] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p = ctx.allocBytes(8, 4);
    p[5] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0x00, 0x01, 0x7F, 0x80 });
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][5] == 0xFF) 0 else 1;
}
