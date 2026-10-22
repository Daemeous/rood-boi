//! zig_diff spec: func_800DFAF8 -> func_800DFAF8.func_800DFAF8 (771A8.c).
//! Inputs: a 16-entry D_800F5910 table in the arena, uniform in most runs
//! (every entry carries the same unk0_0 and unk0_5, so arg1's bytes match
//! for every i) and random in the rest; arg1 whose low byte and byte 2 match
//! the table or miss it (values 32-255 never equal a 5-bit field); arg2 zero
//! and nonzero, with edges. func_8008D2C0 fills the four sp10 entries with
//! values that match the odd table entries for some calls and random i16
//! values for others, so the second test passes and fails in both of its
//! operands.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DFAF8));

pub const classes = [_][:0]const u8{ "byte-miss", "hi-miss", "arg2-zero", "arg2-nonzero" };

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, sp: anytype) c_int {
        const arr: *[4]v.func_8008D2C0_t = @ptrCast(@alignCast(sp));
        const tab: [*]v.D_800F5910_t = @ptrCast(v.g.D_800F5910().*.?);
        for (arr, 0..) |*m, j| {
            const k: usize = if (ctx.value(u1) == 1) 4 * j + 1 else 4 * j + 3;
            m.unk0 = fieldWord(ctx, &tab[k], "unk0_0", ctx.value(u2) != 0);
            m.unk2 = fieldWord(ctx, &tab[k], "unk0_5", ctx.value(u2) != 0);
        }
        return ctx.value(c_int);
    }
};

/// A value whose `>> 7` is the entry's field when `match`, else a random i16.
fn fieldWord(ctx: *zd.Ctx, e: *v.D_800F5910_t, comptime name: []const u8, match: bool) i16 {
    if (!match) return ctx.value(i16);
    const f: i32 = psx.getBits(e, name);
    return @intCast((f << 7) | @as(i32, ctx.value(u7)));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const tab = ctx.alloc([16]v.D_800F5910_t);
    v.g.D_800F5910().* = &tab[0];
    const lo: u5 = ctx.int(u5);
    const hi: u5 = ctx.int(u5);
    const uniform = !ctx.chance(3);
    for (0..16) |k| {
        psx.setBits(&tab[k], "unk0_0", if (uniform) lo else ctx.int(u5));
        psx.setBits(&tab[k], "unk0_5", if (uniform) hi else ctx.int(u5));
    }
    const lo8: u8 = if (ctx.chance(3)) ctx.range(u8, 32, 255) else lo;
    const hi8: u8 = if (ctx.chance(3)) ctx.range(u8, 32, 255) else hi;
    const arg1: c_uint = (ctx.int(c_uint) & 0xFF00_FF00) | @as(c_uint, lo8) | (@as(c_uint, hi8) << 16);
    const arg2: c_int = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, 7, 8, std.math.minInt(c_int) });
    return .{ ctx.int(c_int), arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const tab: [*]v.D_800F5910_t = @ptrCast(v.g.D_800F5910().*.?);
    const b0: u8 = @truncate(args[1]);
    const b2: u8 = @truncate(args[1] >> 16);
    if (b0 != psx.getBits(&tab[0], "unk0_0")) return 0;
    if (b2 != psx.getBits(&tab[0], "unk0_5")) return 1;
    return if (args[2] == 0) 2 else 3;
}
