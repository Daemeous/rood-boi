//! Clears the 0xFE and 0xFD markers in the 32-byte table vs_render_legacyTexturePageReservations
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_advanceTexturePageRetirements (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Maps marker 0xFE in vs_render_legacyTexturePageReservations[0..32] to 0 and marker 0xFD to 0xFE.
pub fn vs_render_advanceTexturePageRetirements() callconv(.c) void {
    const table = v.g.vs_render_legacyTexturePageReservations();
    var i: usize = 0;
    while (i < 32) : (i += 1) {
        if (table[i] == 0xFE) {
            table[i] = 0;
        }
        if (table[i] == 0xFD) {
            // C stores -2 into a u8 element: 0xFE.
            table[i] = 0xFE;
        }
    }
}
