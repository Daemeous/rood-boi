//! Resets the weapon-model flags for a battle actor's two weapon models (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009D88C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the two weapon models of entry arg0's weapon slot to their flag-dependent values (0x40 or 0) and clears their byte 13.
pub fn func_8009D88C(arg0: c_int) callconv(.c) void {
    // The C does not bound arg0 against the 17-entry table: psx.at does not check either.
    const temp = psx.at(v.g.D_800F4538(), arg0).* orelse return;

    var i: c_int = 0;
    while (i < 2) : (i += 1) {
        const model = psx.at(v.g.vs_battle_wepModels(), @as(c_int, temp.unk0.unkF) *% 2 +% i).*;
        if (model) |m| {
            if (m.unkD != 0) {
                if (psx.getBits(m, "unk8_4") != 0) {
                    m.unk11 = 0x40;
                    m.unk12 = 0x40;
                } else {
                    m.unk11 = 0;
                    m.unk12 = 0;
                }
                m.unk13 = 0;
            }
        }
    }
}
