//! zig_diff spec: func_800E65DC -> pc.fill_actor_scratch.fillActorScratch (D2: pc/src/actor_update.c).
//! No inputs: the function only passes the scratchpad base (0x1F800000) to
//! pc_actor_scratch, whose stub records the argument.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").fillActorScratch));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
