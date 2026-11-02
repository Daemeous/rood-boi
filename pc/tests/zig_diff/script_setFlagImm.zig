//! zig_diff spec: vs_battle_script_setFlagImm -> script_set_flag_imm.script_setFlagImm (4A0A8.c).
//! Inputs: a 4-byte opcode with every operator (script[1] high nibble 0..15),
//! the flag's high bits in its low nibble and script[2], and an immediate
//! (0, 1, 0x80, 0xFF, random); never 0 for / and % (the C traps). The
//! getStateFlag stub returns 0, 1, 0x7F, 0x80, 0xFF or any byte, so the sums
//! and products wrap.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setFlagImm));

pub const classes = [_][:0]const u8{ "add", "sub", "mul", "div", "rem", "and", "or", "store", "none" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(4, 1);
    s[1] = (ctx.range(u8, 0, 15) << 4) | ctx.int(u4);
    s[2] = ctx.int(u8);
    s[3] = ctx.pick(u8, &.{ 0, 1, 2, 0x80, 0xFF, 7 });
    const op = s[1] >> 4;
    if ((op == 3 or op == 4) and s[3] == 0) s[3] = ctx.edge(u8, &.{ 1, 3, 0xFF });
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @min(args[0][1] >> 4, 8);
}

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: c_short) u8 {
        _ = id;
        const edges = [_]u8{ 0, 1, 0x7F, 0x80, 0xFF };
        const r = ctx.value(u16);
        return if (r & 1 != 0) edges[(r >> 1) % edges.len] else @truncate(r >> 8);
    }
};
