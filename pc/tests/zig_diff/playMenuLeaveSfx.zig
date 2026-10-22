//! zig_diff spec: vs_battle_playMenuLeaveSfx -> play_menu_leave_sfx.playMenuLeaveSfx (573B8.c).
//! The C body is one call, vs_main_playSfxDefault(0x7E, 6), with no branches and
//! no inputs: the function takes no arguments and reads no state. The spec
//! therefore checks the call's arguments and order in the trace. The callee is a
//! void stub that changes nothing the C reads: a sound-effect request only sets
//! sound-driver state, which this unit never reads back.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").playMenuLeaveSfx));

pub const classes = [_][:0]const u8{"menu-leave"};

pub const stubs = struct {
    pub fn vs_main_playSfxDefault(ctx: *zd.Ctx, id: anytype, volume: anytype) void {
        // A sound request: it writes nothing this unit reads back, and it
        // draws no stub values (the trace records id and volume).
        _ = ctx;
        _ = id;
        _ = volume;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
