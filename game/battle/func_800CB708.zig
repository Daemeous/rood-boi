//! Screen-effect follow-up: runs the pending screff2 steps (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CB708 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the pending screen-effect steps of the screff2 state: the load, the warp, the tiles and the border.
pub fn func_800CB708() callconv(.c) void {
    if (v.g.vs_effects_screenEffectOverlay().*) |state| {
        if (state.unk0 != 0) {
            _ = c._loadScreff2(0);
            return;
        }
        // *(int*)&unk4: the 32-bit word at offset 4 (unk4 and unk6 together).
        if (psx.byteAt(i32, state, 4).* != 0x1000) {
            c.pc_screff2_warp();
        }
        if (state.unk2 != 0) {
            c.pc_screff2_tiles();
        }
        if (state.unk8 != 0) {
            c.pc_screff2_border();
        }
    }
}
