//! zig_diff spec: vs_battle_playMenuChangeSfx -> play_menu_change_sfx.playMenuChangeSfx (573B8.c).
//! The C body is one call, vs_main_playSfxDefault(0x7E, 4), with no branches and no
//! globals or objects read. The inputs are therefore empty: the zero-argument call.
//! The recording stub for vs_main_playSfxDefault compares the argument values, their
//! order and the call itself against the port. There is one input class, since every
//! run takes the same path.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_573B8");
const c = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").playMenuChangeSfx));

pub const classes = [_][:0]const u8{"always"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
