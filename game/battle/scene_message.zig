//! The scene message window (text box 7): action names and battle messages
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_displaySceneMessage (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Opens the name window (text box 7) on scene message `message` about `action`; `held` keeps it up 120 ticks.
pub fn displaySceneMessage(message: c_int, action: c_int, held: c_int) callconv(.c) void {
    // ROOD_PC: a name window held past a skipped wind-up is closed first (pc/src/charge_skip.inc).
    c.pc_charge_skip_name(1);
    psx.put(v.g.nameWindowText(), c.vs_main_allocHeapR(112));
    v.g.nameWindowHeld().* = psx.conv(u8, held);
    if (v.g.nameWindowHeld().* != 0) {
        v.g.nameWindowTimer().* = 0x78;
    }

    c.vs_battle_initTextBox(7, 0x304, 0, 1, 0, 3, 0, 0);

    v.g.nameWindowText().*.?[0] = 0x00F8 | (0x04FB << 16);
    // vs_main_actions[action].name: the address only, the C does not check action < 256.
    psx.put(&v.g.stringContext().strings[0], &psx.at(v.g.actions(), action).name);

    if (message == 12) {
        _ = c.vs_battle_printf(@ptrCast(psx.at(v.g.nameWindowText().*.?, 1)),
            @ptrCast(psx.at(v.g.menuStrings(), c.VS_menuStrings_OFFSET_spellMastered)));
    } else if (message == 11) {
        psx.setBits(&v.g.textBoxes()[7].unk0, "unk0_8", 0);
        const type_: c_int = psx.getBits(psx.at(v.g.actions(), action), "type");
        _ = c.vs_battle_printf(@ptrCast(psx.at(v.g.nameWindowText().*.?, 1)),
            @ptrCast(psx.at(v.g.menuStrings(), psx.at(v.g.menuStrings(), type_ +% 22).*)));
    } else {
        if (action != 0) {
            // D_800EBF58[(action - 0x1CA) * 2]: a 12-byte row's address, before the table for action < 0x1CA.
            psx.put(&v.g.stringContext().strings[0], psx.at(v.g.D_800EBF58(), (action -% 0x1CA) *% 2));
        }
        _ = c.vs_battle_printf(@ptrCast(v.g.nameWindowText().*.?),
            @ptrCast(psx.at(v.g.sceneMessages(), psx.at(v.g.sceneMessages(), message).*)));
    }

    _ = c.vs_battle_setTextBox(7, @ptrCast(v.g.nameWindowText().*.?));

    // ROOD_PC: HUD elements (pc/src/hud/hud_bridge.c): the name window opened.
    const Hook = *const fn (c_int, c_int) callconv(.c) void;
    if (psx.weak(Hook, "pc_hud_name_window")) |hook| hook(message, action);
}
