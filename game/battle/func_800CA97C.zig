//! Clears the ability window state (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CA97C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_effects_screenEffectOverlay = NULL; D_800F4ED8 zeroed (memset), then its third entry 0x1000.
pub fn func_800CA97C() callconv(.c) void {
    v.g.vs_effects_screenEffectOverlay().* = null;
    _ = c.memset(@as(?*anyopaque, @ptrCast(v.g.D_800F4ED8())), @as(c_int, 0), @as(usize, @sizeOf([8]i16)));
    v.g.D_800F4ED8()[2] = 0x1000;
}
