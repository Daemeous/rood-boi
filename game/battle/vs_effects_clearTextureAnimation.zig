//! Clears a sprite state (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_clearTextureAnimation (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// The sprite state (a func_800D6CF_t) is zeroed whole: the PS1 cleared its eight words, which held everything.
pub fn vs_effects_clearTextureAnimation(sprite: ?*anyopaque) callconv(.c) void {
    @memset(@as([*]u8, @ptrCast(sprite.?))[0..@sizeOf(v.func_800D6CF_t)], 0);
}
