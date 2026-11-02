//! zig_diff spec: func_800B8290 -> func_800B8290.func_800B8290 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena with random bytes, its selector
//! byte [3] (zero, one, the sign bit set, 0x7F, 0xFF) and its slot byte [10]
//! (0xFF, which takes the -1 path, and 0xFE, 0x80, 0x7F, 0, 1), and an unused
//! short. vs_battle_getShort returns any 16-bit value. func_800BFE50 returns a
//! table index below 24 (D_800F4B70 has 24 entries), a third of the time with
//! bits above 16 set that the C's u_short drops. The two callees func_800A9F64
//! and func_800A9FD0 copy the vector they get (vx, vy, vz: the C leaves pad
//! unset and they do not read it) to the scratchpad, so its fields are
//! compared, each at its own offset so a swapped callee shows.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8290));

pub const classes = [_][:0]const u8{ "sel-zero-slot-ff", "sel-zero-slot-other", "sel-nonzero-slot-ff", "sel-nonzero-slot-other" };

fn spill(off: usize, p: anytype) void {
    const src: *const [3]i16 = @ptrCast(@alignCast(p));
    const dst: *[3]i16 = @ptrFromInt(zd.scratchpad + off);
    dst.* = src.*;
}

pub const stubs = struct {
    /// The C indexes D_800F4B70 with this value narrowed: keep it below the 24 entries.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const actor: c_int = ctx.value(u8) % 24;
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }

    pub fn func_800A9F64(ctx: *zd.Ctx, a: anytype, p: anytype, b: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        spill(0xF00, p);
    }

    pub fn func_800A9FD0(ctx: *zd.Ctx, a: anytype, p: anytype, b: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        spill(0xF10, p);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(16, 16));
    buf[3] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[10] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0x80, 0x7F, 0, 1 });
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const a = args[0];
    const sel: usize = if (a[3] != 0) 2 else 0;
    return sel + (if (a[10] == 0xFF) @as(usize, 0) else 1);
}
