//! Clears one bit in the i32 flag array at vs_effects_tokenBitmaps (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_releaseEffectToken (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears flag bit `arg0` in the word array vs_effects_tokenBitmaps (the word index rounds toward zero for negative bits).
pub fn vs_effects_releaseEffectToken(arg0: c_int) callconv(.c) void {
    const ptr = v.g.vs_effects_tokenBitmaps();
    var var_v1: c_int = arg0;
    if (arg0 < 0) {
        var_v1 = arg0 +% 0x1F;
    }
    var_v1 >>= 5;
    // The shift amount can be negative for arg0 in -31..-1 (UB in C); x86 masks it to its low 5 bits, so truncate.
    const shift: u5 = @truncate(@as(c_uint, @bitCast(arg0 -% (var_v1 << 5))));
    const bit: c_int = @bitCast(@as(c_uint, 1) << shift);
    psx.at(ptr, var_v1).* &= ~bit;
}
