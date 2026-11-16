//! Sets the battle camera's per-mode value in the battle state (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_storePreferredActionCameraDistance (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores 0x600 or 0x900 in the battle state's unk2C0C by the camera's delta mode (modes 0-3).
pub fn vs_field_storePreferredActionCameraDistance() callconv(.c) void {
    const mode: c_int = v.g.vs_battle_cameraCurrentSpherical().delta.mode;
    switch (mode) {
        0, 1 => v.g.vs_battle_actionState().*.?.unk2C0C = 0x600,
        2, 3 => v.g.vs_battle_actionState().*.?.unk2C0C = 0x900,
        else => {},
    }
}
