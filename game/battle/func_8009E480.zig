//! Checks whether the first battle actor's animation is one of the two special animation ids (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: func_8009E480 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns 1 when the first actor's animationId is 0x63 or 0xC7, else 0.
pub fn func_8009E480() callconv(.c) c_int {
    const actor = v.g.D_800F4538().*[0].?;
    const id: c_int = actor.animationId;
    if (id == 0x63) return 1;
    return @intFromBool(id == 0xC7);
}
