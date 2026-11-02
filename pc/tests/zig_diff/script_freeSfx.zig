//! zig_diff spec: vs_battle_script_freeSfx -> script_free_sfx.script_freeSfx (4A0A8.c).
//! Inputs: a 4-byte arena buffer whose byte 1 is random (so the value passed on
//! to vs_main_freeSfx covers the full u8 range, including bytes >= 128 that a
//! signed read would widen differently), any c_short arg1 (unused by the C), and
//! D_800F4C2C either exactly 2 (half the runs, so the `== 2` result is 4) or
//! other values around it. The callee stub rewrites D_800F4C2C about half the
//! time and returns an arbitrary c_int that the C ignores, so a port that reads
//! the global before the call, or keeps a stale copy, is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_freeSfx));

pub const classes = [_][:0]const u8{ "global-two", "global-other" };

pub const stubs = struct {
    pub fn vs_main_freeSfx(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        // The real callee can change the global the C reads back after the call.
        if (ctx.value(u1) == 1) v.g.D_800F4C2C().* = ctx.value(u8);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 0x82, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
