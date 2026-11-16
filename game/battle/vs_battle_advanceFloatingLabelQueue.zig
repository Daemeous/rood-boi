//! Clears byte unkB of a battle state record and decrements its 16-bit counter unk6 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: vs_battle_advanceFloatingLabelQueue (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Zeroes the record's unkB byte and decrements its unk6 counter.
pub fn vs_battle_advanceFloatingLabelQueue(arg0: ?*v.D_800F1BAC_t) callconv(.c) void {
    const s = arg0.?;
    s.unkB = 0;
    s.unk6 = psx.conv(i16, @as(c_int, s.unk6) -% 1);
}
