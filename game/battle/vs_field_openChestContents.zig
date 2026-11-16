//! The room point's lock message, or its chest (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_openChestContents (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Camera mode 3; a point with a key or sigil shows its message (1 -> 2, 2 -> 1, 3 -> 3) and calls func_8006C2FC;
/// else its unk10 below 0x40 hands the loot after it to vs_menu_frameworkOpenLootEntryMenu, from 0x40 up the item unk10 - 0x40 to
/// vs_menu_frameworkOpenChestStorageMenu, then vs_field_pauseFieldForMenu.
pub fn vs_field_openChestContents() callconv(.c) void {
    const kind: c_int = psx.get(v.g.vs_field_interactionFocus().unk0).?.keyOrSigil;
    v.g._cameraMode().* = 3;
    if (kind != 0) {
        // Uninitialised in the original when kind is not 1..3 (no default case).
        var message: c_int = 0;
        switch (kind) {
            1 => message = 2,
            2 => message = 1,
            3 => message = 3,
            else => {},
        }
        c.vs_battle_displaySceneMessage(message, psx.get(v.g.vs_field_interactionFocus().unk0).?.unkE, 1);
        c.func_8006C2FC();
    } else {
        const item = psx.get(v.g.vs_field_interactionFocus().unk0).?.unk10;
        if (item < 0x40) {
            c.vs_menu_frameworkOpenLootEntryMenu(@ptrCast(psx.at(psx.get(v.g.vs_field_interactionFocus().unk0).?, 1)));
        } else {
            c.vs_menu_frameworkOpenChestStorageMenu(@as(c_int, item) -% 0x40);
        }
        c.vs_field_pauseFieldForMenu();
    }
}
