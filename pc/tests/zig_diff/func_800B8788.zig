//! zig_diff spec: func_800B8788 -> func_800B8788.func_800B8788 (4A0A8.c).
//! Inputs: a 16-byte random block as arg0 (only bytes 1, 2 and 7-9 are read,
//! byte 1 through vs_battle_getShort), and a random short arg1 (unused by the C).
//! The tail bytes 7-9 are chosen by mode: all zero, only byte 9 nonzero, byte 9
//! zero with byte 7 or 8 nonzero, or byte 9 nonzero with byte 7 or 8 nonzero.
//! Nonzero tail bytes come from 1, 0x80 and 0xFF, so both the sign-bit and the
//! low-bit cases occur. vs_battle_getShort and func_800BFE50 are default stubs: the
//! C only passes their results on. func_8009F940 copies what its two pointers
//! hold into an arena log (all four bytes of the first, script[3..6]; the
//! second's first three, script[7..9]: the C leaves its fourth unset), so the
//! stack copies are compared; null pointers log as zeros.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8788));

/// Set by prepare: the arena log func_8009F940 copies its pointers' bytes to.
var log: *[7]u8 = undefined;

fn bytesOf(p: anytype) ?[*]const u8 {
    return switch (@typeInfo(@TypeOf(p))) {
        .null => null,
        .optional => if (p) |q| @ptrCast(q) else null,
        .pointer => if (@intFromPtr(p) == 0) null else @ptrCast(p),
        else => @compileError("pointer expected"),
    };
}

pub const stubs = struct {
    pub fn func_8009F940(ctx: *zd.Ctx, a: anytype, flag: anytype, p: anytype, q: anytype) void {
        _ = ctx;
        _ = a;
        _ = flag;
        log.* = @splat(0);
        if (bytesOf(p)) |b| @memcpy(log[0..4], b[0..4]);
        if (bytesOf(q)) |b| @memcpy(log[4..7], b[0..3]);
    }
};

pub const classes = [_][:0]const u8{ "tail-zero", "only-byte9", "byte9-zero-7or8", "tail-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    log = ctx.alloc([7]u8);
    const bytes = ctx.allocBytes(16, 16);
    const mode = ctx.range(u8, 0, 3);
    switch (mode) {
        0 => {
            bytes[7] = 0;
            bytes[8] = 0;
            bytes[9] = 0;
        },
        1 => {
            bytes[7] = 0;
            bytes[8] = 0;
            bytes[9] = ctx.edge(u8, &.{ 1, 0x80, 0xFF, 0x7F });
        },
        2 => {
            bytes[7] = ctx.edge(u8, &.{ 1, 0x80, 0xFF, 0x7F });
            bytes[8] = ctx.pick(u8, &.{ 0, 0x80 });
            bytes[9] = 0;
        },
        else => {
            bytes[7] = ctx.edge(u8, &.{ 1, 0x80, 0xFF, 0x7F });
            bytes[8] = ctx.pick(u8, &.{ 0, 0x80 });
            bytes[9] = ctx.edge(u8, &.{ 1, 0x80, 0xFF, 0x7F });
        },
    }
    const arg1 = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    return .{ bytes, arg1 };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    if (p[7] == 0 and p[8] == 0 and p[9] == 0) return 0;
    if (p[9] == 0) return 2;
    if (p[7] == 0 and p[8] == 0) return 1;
    return 3;
}
