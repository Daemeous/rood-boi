//! An effect-script opcode: stores a script halfword into a register byte (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_setScriptRegister (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// unk10[next byte] = (u8) next halfword; returns 1.
pub fn vs_effects_setScriptRegister(script: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const s = script.?;
    // Object order: the value (vs_effects_readScriptHalfword) before the index (vs_effects_readScriptByte).
    const value = c.vs_effects_readScriptHalfword(@ptrCast(s));
    const index = c.vs_effects_readScriptByte(@ptrCast(s));
    psx.at(&s.unk10, index).* = psx.conv(u8, value); // the byte index can pass unk10's 4 (the C does not check)
    return 1;
}
