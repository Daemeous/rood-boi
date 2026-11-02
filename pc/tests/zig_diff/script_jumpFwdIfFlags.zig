//! zig_diff spec: vs_battle_script_jumpFwdIfFlags -> script_jump_fwd_if_flags.script_jumpFwdIfFlags (4A0A8.c).
//! Inputs: a 6-byte opcode with every test (script[1] high nibble 0..15) and
//! the two flag ids from script[1..3]. The getStateFlag stub returns the first
//! flag's value again for the second, one either side of it, or any byte, so
//! every test is seen equal, below and above; getShort returns 0, -1, 0x7FFF,
//! -0x8000 or any offset (a negative one jumps back).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwdIfFlags));

pub const classes = [_][:0]const u8{ "gt", "lt", "ne", "eq", "ge", "le", "always" };

var first: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(6, 1);
    s[1] = (ctx.range(u8, 0, 15) << 4) | ctx.int(u4);
    s[2] = ctx.int(u8);
    s[3] = ctx.int(u8);
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @min(args[0][1] >> 4, 6);
}

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: c_short) u8 {
        _ = id;
        if (ctx.calls == 0) {
            const edges = [_]u8{ 0, 1, 0x80, 0xFF };
            const r = ctx.value(u16);
            first = if (r & 1 != 0) edges[(r >> 1) % edges.len] else @truncate(r >> 8);
            return first;
        }
        return switch (ctx.value(u8) % 4) {
            0 => first,
            1 => first +% 1,
            2 => first -% 1,
            else => ctx.value(u8),
        };
    }
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return switch (ctx.value(u8) % 5) {
            0 => 0,
            1 => -1,
            2 => 0x7FFF,
            3 => -0x8000,
            else => ctx.value(c_short),
        };
    }
};
