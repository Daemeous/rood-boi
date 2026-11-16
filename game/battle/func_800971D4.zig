//! Advances the 7 flag in each entry of the table at *vs_battle_floatingLabels to 6, for vs_battle_floatingLabelCount entries
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_800971D4 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Changes every table entry whose byte 8 is 7 to 6, over the count in vs_battle_floatingLabelCount.
pub fn func_800971D4() callconv(.c) void {
    // The table pointer is read once, before the loop, as the C's `p` is.
    const first = v.g.vs_battle_floatingLabels().*;
    var i: c_int = 0;
    // The count is re-read each iteration, as the C's loop condition does.
    while (i < v.g.vs_battle_floatingLabelCount().*) : (i += 1) {
        const base: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(first.?));
        const p = psx.at(base, i);
        if (p.unk8.u8[0] == 7) {
            p.unk8.u8[0] = 6;
        }
    }
}
