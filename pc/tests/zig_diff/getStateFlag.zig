//! zig_diff spec: vs_battle_getStateFlag -> get_state_flag.getStateFlag (4A0A8.c).
//! The C has no branches or comparisons: it reads one byte of
//! vs_main_stateFlags at a short index. The inputs cover indices 0 up to the
//! struct's last byte (the low bytes, both sides of 255/256, the last byte)
//! and a few negative indices (-1, -16), where the C reads just before the
//! global. The global's bytes are random and compared with the result.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getStateFlag));

const size = @sizeOf(v.vs_main_flags_t);

pub const classes = [_][:0]const u8{ "low", "mid", "negative", "last" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Only the listed indices: each stays in the struct, or just before it.
    const idx = ctx.edge(c_short, &.{ 0, 1, 127, 128, 255, 256, 257, 511, size - 1, size - 2, -1, -2, -16 });
    return .{idx};
}

pub fn classify(args: Args) usize {
    const i = args[0];
    if (i < 0) return 2;
    if (i == size - 1) return 3;
    if (i < 256) return 0;
    return 1;
}
