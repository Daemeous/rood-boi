//! zig_diff spec: vs_battle_setStateFlag -> set_state_flag.setStateFlag (4A0A8.c).
//! Inputs: a state index in [0, 1087], the size of vs_main_stateFlags, with the
//! edges 0, 255, 256 and 1087 drawn often, and a random byte value (sign bit set
//! about half the time). The two classes split the index at 256, so the 8-bit
//! truncation of the index and the byte-value cases are both reached. Negative
//! indices are not generated: they would address bytes before the global, outside
//! the memory this spec can vouch for.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setStateFlag));

pub const classes = [_][:0]const u8{ "index-below-256", "index-256-and-up" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const flags = v.g.vs_main_stateFlags();
    _ = flags;
    const id: c_short = if (ctx.chance(2))
        ctx.edge(c_short, &.{ 0, 1, 255, 256, 257, 1087 })
    else
        ctx.range(c_short, 0, 1087);
    const value: u8 = ctx.int(u8);
    return .{ id, value };
}

pub fn classify(args: Args) usize {
    return if (args[0] < 256) 0 else 1;
}
