//! Allocates the first free bit in the four-word mask at vs_effects_tokenBitmaps (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_allocateEffectToken (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the lowest clear bit 1..31 across the four words at vs_effects_tokenBitmaps and returns its index (word * 32 + bit), or 0 if none is free.
pub fn vs_effects_allocateEffectToken() callconv(.c) c_int {
    const words = v.g.vs_effects_tokenBitmaps();
    var result: c_int = 0;
    var i: c_int = 0;
    outer: while (i < 4) : (i += 1) {
        const idx: usize = @intCast(i);
        const temp: i32 = words[idx];
        if (temp == -2) continue;

        var j: c_int = 1;
        while (j < 32) : (j += 1) {
            // j is 1..31, so the shift amount fits u5; 1 << 31 is the sign bit as in C.
            const bit: i32 = @bitCast(@as(u32, 1) << @as(u5, @intCast(j)));
            if ((temp & bit) == 0) {
                words[idx] = temp | bit;
                // i < 4 and j < 32: the index cannot overflow.
                result = (i << 5) + j;
                break :outer;
            }
        }
    }
    return result;
}
