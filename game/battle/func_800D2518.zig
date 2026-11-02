//! Starts the camera from its first keyframe (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D2518 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The current camera (D_800F54D8) = the first keyframe (D_800F5520[0]), every byte.
pub fn func_800D2518() callconv(.c) void {
    v.g.D_800F54D8().* = v.g.D_800F5520()[0];
}
