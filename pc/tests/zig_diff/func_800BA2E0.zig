//! zig_diff spec: func_800BA2E0 -> func_800BA2E0.func_800BA2E0 (4A0A8.c).
//! Inputs: an arena byte buffer whose [1] is an event index (0-5, with 2 often
//! so the event branch is taken), [2] and [3] and [4] random bytes (with the
//! sign bit set half the time for [2]); D_800F4C08 zero or not, D_800F4C4C any
//! i16. vs_main_playSfx is a recording stub that sometimes overwrites
//! D_800F4C4C, so a store moved across the call shows up.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA2E0));

pub const classes = [_][:0]const u8{ "early-return", "store-and-play", "not-event-2" };

pub const stubs = struct {
    pub fn vs_main_playSfx(ctx: *zd.Ctx, arg0: anytype, id: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg0;
        _ = id;
        _ = arg2;
        _ = arg3;
        if (ctx.value(u1) == 1) v.g.D_800F4C4C().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    buf[1] = ctx.edge(u8, &.{ 0, 1, 2, 2, 2, 3, 4, 5 });
    buf[2] = ctx.pick(u8, &.{ 0x80, 0xFF, 0, 0x7F });
    buf[3] = ctx.int(u8);
    buf[4] = ctx.int(u8);
    v.g.D_800F4C08().* = ctx.edge(u8, &.{ 0, 0, 1, 0x80, 0xFF });
    v.g.D_800F4C4C().* = ctx.int(i16);
    return .{buf};
}

pub fn classify(args: Args) usize {
    const buf = args[0];
    if (buf[1] != 2) return 2;
    return if (v.g.D_800F4C08().* == 0) 0 else 1;
}
