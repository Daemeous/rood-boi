//! Stores two per-model fields for the weapon model in slot arg0 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009F898 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets unk5C0 and the unk8_6 bit of the weapon model in slot arg0 (indexed as arg0 * 2), if present.
pub fn func_8009F898(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    const model = psx.at(v.g.vs_battle_wepModels(), arg0 *% 2).*;

    if (model) |m| {
        m.unk5C0 = psx.conv(u8, arg2);
        psx.setBits(m, "unk8_6", psx.conv(u1, arg1));
    }
}
