//! The battle menu's top level (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// `(enabled >> row) & 1`: the C shifts by a u_int, which x86 takes modulo 32 (a row past 31 only comes from
/// a stale submenu state).
fn rowEnabled(enabled: c_uint, row: c_uint) bool {
    return ((enabled >> @truncate(row)) & 1) != 0;
}

/// The menu text at an offset in vs_battle_menuStrings (its own entries are the offsets).
fn menuText(index: c_int) [*c]u8 {
    const strings = v.g.menuStrings();
    return @ptrCast(psx.at(strings, psx.at(strings, index).*));
}

/// orig: func_800C930C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// `mode` nonzero opens the top-level menu (the remembered or first enabled row; ROOD_PC: the modern menu
/// when it takes over); 0 steps it: the rows slide in, then up / down move over enabled rows, confirm returns
/// the row + 1, cancel -1, and a disabled row explains itself in text box 7. ROOD_PC: fast travel's rows and
/// the modern menu's own top level.
pub fn func_800C930C(mode_arg: c_int) callconv(.c) c_int {
    var mode = mode_arg;
    var selected: c_uint = v.g.vs_battle_submenuStates()[0];
    const items: [*c]v.vs_battle_menuItem_t = v.g.vs_battle_menuItems().*; // copied, not read: the C reads through it later
    var enabled: c_uint = v.g.vs_main_settings().menuFlags;
    var mask: c_uint = undefined;
    enabled |= c.pc_fast_travel_menu_bits();
    if (mode == 0 and c.pc_ui_menu_active() != 0) {
        return c.pc_ui_menu_top();
    }
    if (mode != 0) {
        if (mode == 2 and v.g.vs_main_settings().cursorMemory == 0) {
            selected = 0;
        }
        while (!rowEnabled(enabled, selected)) {
            selected = (selected +% 1) % 10;
        }
        v.g.vs_battle_submenuStates()[0] = psx.conv(u8, selected);
        c.vs_battle_rMemzero(@ptrCast(v.g.vs_battle_menuItems().*), 0xB24);
        if (c.pc_ui_menu_open(mode) != 0) {
            v.g.D_800F4CC0().* = 0;
            return 0;
        }
        mask = 9;
        mode = 0;
        while (mode < 10) : (mode += 1) {
            if (rowEnabled(enabled, @intCast(mode))) {
                items[@intCast(mode)].animationStep = psx.conv(u8, mask);
                mask += 1;
            }
        }
        v.g.D_800F4CC0().* = @bitCast(-%mask);
        return 0;
    }
    if (v.g.D_800F4CC0().* < 0) {
        v.g.D_800F4CC0().* += 1;
        selected = 0;
        while (mode < 10) : (mode += 1) { // mode is 0 here
            if (rowEnabled(enabled, @intCast(mode))) {
                var step = items[@intCast(mode)].animationStep;
                if (step != 0) {
                    step -= 1;
                    items[@intCast(mode)].animationStep = step;
                }
                _ = c.vs_battle_setMenuItem(mode, @as(c_int, v.g.vs_battle_rowAnimationSteps()[step]) + 194, @bitCast(selected *% 16 +% 18), 126, if (mode == 10) 24 else 0, menuText(mode));
                selected +%= 1;
                // The original sets these every row.
                mask = @bitCast(v.g.D_800F4EA0().*);
                items[0].unselectable = @intFromBool((mask & c.pc_fast_travel_mask(0xB7)) != 0);
                items[1].unselectable = @intFromBool((mask & 0x15F) != 0);
                items[3].unselectable = @intFromBool((mask & 7) != 0);
                items[5].unselectable = @intFromBool(c.vs_battle_getCurrentSceneId() == 0);
            }
        }
        return 0;
    }
    const pressed: c_uint = v.g.vs_main_buttonsPressed().all;
    if ((pressed & (1 << 5)) != 0) {
        c.vs_battle_playMenuSelectSfx();
        psx.at(items, selected).selected = 1;
        psx.at(items, selected).backgroundWidth = 1;
        v.g.textBoxes()[7].state = 0;
        return @bitCast(selected +% 1);
    }
    psx.at(items, selected).selected = 0;
    if ((pressed & ((1 << 4) | (1 << 6))) != 0) {
        c.vs_battle_playMenuLeaveSfx();
        v.g.textBoxes()[7].state = 0;
        return -1;
    }
    const repeat: c_uint = @bitCast(v.g.vs_main_buttonRepeat().*);
    if ((repeat & (1 << 12)) != 0) {
        while (true) {
            selected = (selected +% 9) % 10;
            if (rowEnabled(enabled, selected)) break;
        }
    }
    if ((repeat & (1 << 14)) != 0) {
        selected = (selected +% 1) % 10;
    }
    while (!rowEnabled(enabled, selected)) {
        selected = (selected +% 1) % 10;
    }
    if (selected != v.g.vs_battle_submenuStates()[0]) {
        c.vs_battle_playMenuChangeSfx();
        v.g.vs_battle_submenuStates()[0] = psx.conv(u8, selected);
    }
    psx.at(items, selected).selected = 1;
    v.g.D_800F4CC0().* = c.vs_battle_drawCursor(v.g.D_800F4CC0().*, (@as(c_int, psx.at(items, selected).y) >> 4) - 1);
    if (psx.at(items, selected).unselectable != 0) {
        if ((v.g.vs_main_buttonsState().* & ((1 << 12) | (1 << 14))) != 0) {
            return 0;
        }
        c.vs_battle_initInformationTextBox(1);
        v.g.textBoxes()[7].state = 11;
        _ = c.vs_battle_setTextBox(7, @ptrCast(v.g.D_800EBA78()));
        mask = 0;
        if (selected == 0) {
            mask = 0xB7;
        }
        if (selected == 1) {
            mask = 0x15F;
        }
        if (selected == 3) {
            mask = 7;
        }
        const conditions: c_uint = @as(c_uint, @bitCast(v.g.D_800F4EA0().*)) & mask;
        mode = 0;
        while (mode < 9) : (mode += 1) {
            if ((conditions & (@as(c_uint, 1) << @intCast(mode))) != 0) {
                break;
            }
        }
        psx.put(&v.g.stringContext().strings[0], @as(*u8, @ptrCast(menuText(mode + 12))));
    } else {
        v.g.textBoxes()[7].state = 0;
    }
    return 0;
}
