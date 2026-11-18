//! Walks a p_file block chain and fills vs_effects_package (pc/src/game/BATTLE/BATTLE.PRG/6F014.c); the format is effect_package.zig's.
const v = @import("views_6F014");
const effect_package = @import("effect_package.zig");

/// orig: vs_effects_bindEffectPackageBlocks (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Parses each block of a p_file in order until the end marker (type 0), advancing by each block's size.
pub fn vs_effects_bindEffectPackageBlocks(arg0: ?*v.p_file_t) callconv(.c) void {
    effect_package.bind(@ptrCast(arg0.?));
}
