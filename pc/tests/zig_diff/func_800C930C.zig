//! zig_diff spec: func_800C930C -> func_800C930C.func_800C930C (5BF94.c).
//! Inputs: mode 1, 2 (with cursor memory or not), 3 or 0; the remembered
//! row 0..9 or past 31 (the C's shift by it is taken modulo 32); the menu
//! flags with at least one of rows 0..9 enabled (none loops forever in the
//! C); vs_battle_menuItems an arena array of ten rows; D_800F4CC0 negative
//! (the rows slide in) or not; the buttons none, confirm, cancel, the
//! repeats up and down, the held state; the rows' unselectable flags and
//! D_800F4EA0 random. The ROOD_PC hooks (fast travel, the modern menu) are
//! stubs returning 0 mostly.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C930C));

pub const classes = [_][:0]const u8{ "open", "slide", "confirm", "cancel", "move" };

var active: c_int = 0;
var opened: c_int = 0;

pub const stubs = struct {
    pub fn pc_ui_menu_active(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return active;
    }
    pub fn pc_ui_menu_open(ctx: *zd.Ctx, mode: anytype) c_int {
        _ = ctx;
        _ = mode;
        return opened;
    }
    pub fn pc_fast_travel_menu_bits(ctx: *zd.Ctx) c_uint {
        return if (ctx.value(u8) < 64) 1 << 9 else 0;
    }
    pub fn pc_fast_travel_mask(ctx: *zd.Ctx, bits: anytype) c_uint {
        return if (ctx.value(u8) < 128) 0 else bits;
    }
    pub fn vs_battle_getCurrentSceneId(ctx: *zd.Ctx) c_int {
        return if (ctx.value(u8) < 128) 0 else ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const rows: *v.vs_battle_menuItem_t = @ptrCast(@alignCast(ctx.allocBytes(10 * @sizeOf(v.vs_battle_menuItem_t), 16)));
    v.g.vs_battle_menuItems().* = rows;
    for (@as([*]v.vs_battle_menuItem_t, @ptrCast(rows))[0..10]) |*row| row.unselectable = ctx.edge(u8, &.{ 0, 0, 1, 2 });
    v.g.D_800F4EA0().* = ctx.edge(i32, &.{ 0, 1, 8, 0x100, -1 });
    v.g.vs_battle_submenuStates()[0] = ctx.edge(u8, &.{ 0, 1, 5, 9, 40 });
    var flags = ctx.int(u16);
    if (flags & 0x3FF == 0) flags |= @as(u16, 1) << @intCast(ctx.range(u4, 0, 9));
    if (ctx.chance(3)) flags = @as(u16, 1) << @intCast(ctx.range(u4, 0, 9));
    v.g.vs_main_settings().menuFlags = flags;
    v.g.vs_main_settings().cursorMemory = ctx.edge(u8, &.{ 0, 1 });
    v.g.D_800F4CC0().* = ctx.edge(i32, &.{ -3, -1, 0, 5 });
    v.g.vs_main_buttonsPressed().all = ctx.edge(u32, &.{ 0, 0, 0, 1 << 5, 1 << 4, 1 << 6 });
    v.g.vs_main_buttonRepeat().* = ctx.edge(i32, &.{ 0, 1 << 12, 1 << 14, (1 << 12) | (1 << 14) });
    v.g.vs_main_buttonsState().* = ctx.edge(i32, &.{ 0, 0, 1 << 12 });
    active = ctx.edge(c_int, &.{ 0, 0, 0, 1 });
    opened = ctx.edge(c_int, &.{ 0, 0, 0, 1 });
    return .{ctx.edge(c_int, &.{ 0, 0, 0, 1, 2, 3 })};
}

pub fn classify(args: Args) usize {
    if (args[0] != 0) return 0;
    if (v.g.D_800F4CC0().* < 0) return 1;
    const pressed = v.g.vs_main_buttonsPressed().all;
    if (pressed & (1 << 5) != 0) return 2;
    if (pressed & ((1 << 4) | (1 << 6)) != 0) return 3;
    return 4;
}
