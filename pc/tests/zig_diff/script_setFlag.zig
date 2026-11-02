//! zig_diff spec: vs_battle_script_setFlag -> script_set_flag.script_setFlag (4A0A8.c).
//! Inputs: a 4-byte opcode with every operator (script[1] high nibble 0..15)
//! and two flag ids from script[1..3]. The getStateFlag stub returns edge
//! bytes (0, 1, 0x7F, 0x80, 0xFF) or any byte; for / and % it never returns 0
//! (the C traps), and for |a - b| both signs of the difference occur.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setFlag));

pub const classes = [_][:0]const u8{ "add", "sub", "mul", "div", "rem", "and", "or", "copy", "abs", "swap", "none" };

var divides = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(4, 1);
    s[1] = (ctx.range(u8, 0, 15) << 4) | ctx.int(u4);
    s[2] = ctx.int(u8);
    s[3] = ctx.int(u8);
    const op = s[1] >> 4;
    divides = op == 3 or op == 4;
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @min(args[0][1] >> 4, 10);
}

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: c_short) u8 {
        _ = id;
        const edges = [_]u8{ 0, 1, 0x7F, 0x80, 0xFF };
        const r = ctx.value(u16);
        const x: u8 = if (r & 1 != 0) edges[(r >> 1) % edges.len] else @truncate(r >> 8);
        return if (divides and x == 0) 3 else x;
    }
};
