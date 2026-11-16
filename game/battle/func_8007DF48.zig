//! Clears the battle flag table at vs_render_legacyTexturePageReservations and sets its first 25 entries to 0xFF (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_8007DF48 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Zeroes the 32-byte flag table, sets its first 25 entries to 0xFF, and clears vs_content_overlayWorkspaceOwner.
pub fn func_8007DF48() callconv(.c) void {
    const table = v.g.vs_render_legacyTexturePageReservations();
    var i: usize = 0;
    while (i < 0x20) : (i += 1) {
        table[i] = 0;
    }
    i = 0;
    while (i < 0x19) : (i += 1) {
        table[i] = 0xFF;
    }
    v.g.vs_content_overlayWorkspaceOwner().* = 0;
}
