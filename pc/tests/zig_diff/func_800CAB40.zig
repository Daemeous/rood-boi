//! zig_diff spec: func_800CAB40 -> func_800CAB40.func_800CAB40 (5BF94.c).
//! Inputs: the shortcut 5 (the item shortcut's menu) or another; the
//! modern-menu cast-cancel hook (a stub here) returning 0 or not; the menu
//! state random (its return state indexes the submenu states by its low
//! nibble); D_800F4FDB 0 (the sound) or not.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CAB40));

pub const classes = [_][:0]const u8{ "shortcut", "cancelled", "return" };

var cancelled: c_int = 0;

pub const stubs = struct {
    pub fn pc_ui_menu_cast_cancelled(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return cancelled;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.vs_battle_shortcutInvoked().* = ctx.pick(u8, &.{ 5, 5, 0, 4, 6 });
    cancelled = ctx.pick(c_int, &.{ 0, 0, 1, -1 });
    v.g.D_800F4FDB().* = ctx.pick(u8, &.{ 0, 0, 1 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.vs_battle_shortcutInvoked().* == 5) return 0;
    return if (cancelled != 0) 1 else 2;
}
