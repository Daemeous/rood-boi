//! zig_diff spec: func_800B9C58 -> func_800B9C58.func_800B9C58 (4A0A8.c).
//! Inputs: a 16-byte arena block as arg0 with a random header. The high
//! nibble of byte 1 picks the switch case (0, 1, 2, or one of the 13 values
//! with no case). Byte 4 is a shift count over the whole byte (a count of 32
//! or more is undefined in the C; both objects shift by the count mod 32,
//! `shl %cl`, and the port does the same), bytes 5-7 are signed-char arguments with
//! both sign bits and the edges, and arg1 is any short. The case-0 iterator
//! stub keeps its own step count in the iterator (unk0, starting at 0). For
//! the first 40 steps it returns 1 at random (so the loop runs zero, one or
//! several times). After that it returns the parity of the count, alternating
//! 0 and 1, so the loop ends for the original (stops on 1) and for a mutant
//! that stops on 0 alike. The getShort stub uses the default random return value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9C58));

pub const classes = [_][:0]const u8{ "case-0", "case-1", "case-2", "no-case" };

pub const stubs = struct {
    /// Initialises the iterator: the C passes it on and never reads it back,
    /// but the real callee writes it. unk0 is the stub's own step count.
    pub fn func_800BB68C(ctx: *zd.Ctx, id: anytype, p: anytype) void {
        _ = id;
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(p));
        s.unk0 = 0;
        s.unk1 = ctx.value(u8);
    }

    /// Advances the iterator and returns the next value. The loop ends on 1
    /// in the original and on 0 in a mutant that tests for 0, so after 40
    /// steps the stub alternates 0 and 1 and both variants terminate.
    pub fn func_800BB788(ctx: *zd.Ctx, p: anytype) c_ushort {
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(p));
        s.unk0 +%= 1;
        s.unk1 = ctx.value(u8);
        if (s.unk0 > 40) return @intCast(s.unk0 & 1);
        if (ctx.value(u8) < 64) return 1;
        return ctx.value(c_ushort);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = ctx.allocBytes(16, 16);
    const hi = ctx.edge(u8, &.{ 0, 1, 2, 3, 7, 15 });
    p[1] = (hi << 4) | (ctx.int(u8) & 0xF);
    p[4] = ctx.edge(u8, &.{ 0, 1, 4, 5, 31, 32, 33, 63, 64, 255 });
    p[5] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    p[6] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    p[7] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return switch (args[0][1] >> 4) {
        0 => 0,
        1 => 1,
        2 => 2,
        else => 3,
    };
}
