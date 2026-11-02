//! zig_diff spec: vs_battle_script_setStringContextInt -> script_set_string_context_int.script_setStringContextInt (4A0A8.c).
//! Inputs: a 4-byte opcode with each mode (script[3] >> 6) and a slot index
//! script[1] in 0..19 (past integers[9] the C writes into strings[]; the
//! struct ends at 20 words). getShort returns 0, 0x3FF, -1 or any id (the
//! & 0x3FF and the +1 at 0x3FF), getStateFlag edge bytes (0x7F, 0x80, 0xFF)
//! for the signed conversions.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setStringContextInt));

pub const classes = [_][:0]const u8{ "byte", "signed-byte", "half", "signed-half" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(4, 1);
    s[1] = ctx.range(u8, 0, 19);
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return args[0][3] >> 6;
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => 0x3FF,
            2 => -1,
            else => ctx.value(c_short),
        };
    }
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: c_short) u8 {
        _ = id;
        const edges = [_]u8{ 0, 0x7F, 0x80, 0xFF };
        const r = ctx.value(u16);
        return if (r & 1 != 0) edges[(r >> 1) % edges.len] else @truncate(r >> 8);
    }
};
