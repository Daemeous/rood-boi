//! zig_diff spec: vs_battle_script_jumpFwdIfFlag -> script_jump_fwd_if_flag.script_jumpFwdIfFlag (4A0A8.c).
//! Inputs: a 6-byte opcode with every test (script[1] high nibble 0..15), the
//! flag id from script[1..2] and the operand script[3]. The getStateFlag stub
//! returns the operand itself, one either side of it, or any byte, so every
//! test is seen equal, below and above; getShort returns 0, -1, 0x7FFF,
//! -0x8000 or any offset (a negative one jumps back).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwdIfFlag));

pub const classes = [_][:0]const u8{ "lt", "gt", "ne", "eq", "ge", "le", "and", "always" };

var operand: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(6, 1);
    s[1] = (ctx.range(u8, 0, 15) << 4) | ctx.int(u4);
    s[2] = ctx.int(u8);
    s[3] = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    operand = s[3];
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @min(args[0][1] >> 4, 7);
}

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: c_short) u8 {
        _ = id;
        return switch (ctx.value(u8) % 4) {
            0 => operand,
            1 => operand +% 1,
            2 => operand -% 1,
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
