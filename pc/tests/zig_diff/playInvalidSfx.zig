//! zig_diff spec: vs_battle_playInvalidSfx -> play_invalid_sfx.playInvalidSfx (573B8.c).
//! The C has no branches and reads no state: it makes one call,
//! vs_main_playSfxDefault(0x7E, 7). The inputs are empty (the function takes no
//! arguments). The recording stub for the callee is compared by its arguments
//! and by the state hash at the call, so a wrong constant, a swapped argument,
//! a dropped call or a doubled call all show as mismatches.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").playInvalidSfx));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
