//! Battle state step: the key-or-sigil gate, then the state flag or the room callback (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateChestContentsSelection (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Advances the battle state one step; returns 1 while the step is not finished, 0 when it is.
pub fn vs_field_updateChestContentsSelection() callconv(.c) c_int {
    if (psx.get(v.g.vs_field_interactionFocus().unk0).?.keyOrSigil != 0) {
        if (c.func_800CB45C() != 0) return 1;
        c.vs_field_resumeFieldAfterMenu();
        return 0;
    }
    c.vs_menu_frameworkUpdateMenuOverlay();
    const temp: *align(1) v.D_800F18EC_t = @ptrCast(c.vs_menu_frameworkActionSelectionState().?);
    if (temp.unk0 == 1) return 1;
    if (temp.unk2 != 0) {
        c.vs_battle_setStateFlag(
            psx.conv(c_short, @as(c_int, psx.get(v.g.vs_field_interactionFocus().unk0).?.unk10) +% 0x3C0),
            1,
        );
    } else {
        c.func_8008D5FC(psx.cptr(c._mpdRoomSectionA, psx.get(v.g.vs_field_interactionFocus().unk0).?));
    }
    c.vs_menu_frameworkReleaseMenuRows();
    c.vs_field_resumeFieldAfterMenu();
    return 0;
}
