//! zig_diff spec: vs_battle_playMenuSelectSfx -> play_menu_select_sfx.playMenuSelectSfx (573B8.c).
//! The C has no branches, no comparisons and no reads: it makes one call,
//! vs_main_playSfxDefault(0x7E, 5). The inputs are empty (the function takes
//! no arguments and reads no state), so the one class is the call itself.
//! The callee is a recording void stub that writes nothing: the trace records
//! its two arguments and a hash of the state at the call, and the mutations
//! change either constant or drop the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
const c = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").playMenuSelectSfx));

pub const classes = [_][:0]const u8{"call"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
