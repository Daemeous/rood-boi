//! Builds a 3-halfword record from a function-local allocation and bumps the
//! global record counter (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_battle_queueWorldLockedLabel (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Fills a record from the three halfwords at `words`, tags it as kind 3, and counts it.
pub fn vs_battle_queueWorldLockedLabel(words: [*c]c_ushort) callconv(.c) c_int {
    const rec: *align(1) v.func_800962E4_t = @ptrCast(c.vs_battle_reserveFloatingLabel().?);
    rec.unkB = 0;
    rec.unk8 = 3;
    rec.unk0 = words[0];
    rec.unk2 = words[1];
    rec.unk4 = words[2];
    v.g.vs_battle_floatingLabelCount().* +%= 1;
    return 0;
}
