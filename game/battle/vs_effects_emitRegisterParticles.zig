//! An effect-script opcode: func_800D2ADC on a register byte (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_emitRegisterParticles (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800D2ADC(script, unk10[next byte], 0, 0, NULL); returns 1.
pub fn vs_effects_emitRegisterParticles(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const s = script.?;
    const index = c.vs_effects_readScriptByte(@ptrCast(s));
    c.func_800D2ADC(@ptrCast(s), psx.at(&s.unk10, index).*, 0, 0, null); // the byte index can pass unk10's 4 (the C does not check)
    return 1;
}
