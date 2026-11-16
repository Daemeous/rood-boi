//! Resets the battle menu and UI state and renders the menu images (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const std = @import("std");
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkInitGameInterface (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears the battle menu and UI state, then renders the menu images from `base`.
pub fn vs_menu_frameworkInitGameInterface(base: ?*anyopaque) callconv(.c) void {
    v.g.D_800EB9AC().* = 0;
    v.g._loadedSubMenu().* = 0;
    v.g.D_800EB9AE().* = 0;
    v.g.vs_battle_hudSlideOffset().* = 0;
    v.g.vs_menu_frameworkUnusedInterfaceFlag().* = 0;
    v.g.vs_effects_screenEffectOverlay().* = null;
    v.g.vs_battle_targetSphere().* = null;
    v.g.vs_render_gimLayers().* = null;
    v.g.vs_battle_menuItems().* = null;
    v.g.vs_menu_textBoxBufferIndex().* = 0;
    v.g.vs_battle_lowerScreenUiState().* = 0;
    v.g.vs_battle_hudHidden().* = 0;
    v.g.vs_battle_hudBattleMode().* = 0;
    v.g.vs_battle_battleModeIndicatorStep().* = 0;
    v.g.D_800EB9D0().s32 = 0xFFFFFF;
    v.g._menuBgUnpackedBuf().* = null;
    v.g.nameWindowText().* = null;
    _ = c.func_800CA97C();

    c.vs_battle_rMemzero(v.g.vs_battle_menuState(), @sizeOf(v.vs_battle_menuState_t));
    v.g.vs_battle_menuState().currentMenu = 0x3F;
    c.vs_battle_rMemzero(v.g.vs_menu_frameworkActionSelection(), @sizeOf(v.D_800F18EC_t));
    c.vs_battle_rMemzero(&v.g.textBoxes()[0], @sizeOf(v.vs_battle_textBox));
    c.vs_battle_rMemzero(v.g.vs_battle_submenuStates(), @sizeOf([16]u8));
    v.g.vs_menu_frameworkMenuMemory().* = std.mem.zeroes(v.D_800F4EE8_t);
    v.g.vs_menu_activeTextBoxOrderingTable().* = @as([*]u32, @ptrCast(v.g.vs_menu_textBoxOrderingTables()));

    c.vs_battle_renderImage(0x340, base, 0xE00040);
    c.vs_battle_renderImage(0x380, @as(?*anyopaque, @ptrFromInt(@intFromPtr(base) +% 0x7000)), 0xE00040);
    c.vs_battle_renderImage(0x4203C0, @as(?*anyopaque, @ptrFromInt(@intFromPtr(base) +% 0xE000)), 0x9E0040);
    _ = c.DrawSync(0);

    const ot: [*c]c.u_long = @ptrCast(v.g.vs_menu_activeTextBoxOrderingTable().*.?);
    _ = c.ClearOTag(ot, 0x22);
}
