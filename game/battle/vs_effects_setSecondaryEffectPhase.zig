//! Stores a 16-bit value in the unkD1C.unk3A field of a BATTLE record (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: vs_effects_setSecondaryEffectPhase (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the low 16 bits of `value` in the record's unkD1C.unk3A field.
pub fn vs_effects_setSecondaryEffectPhase(arg0: c_int, arg1: ?*v.D_800F53B8_t) callconv(.c) void {
    arg1.?.context.unk3A = psx.conv(i16, arg0);
}
