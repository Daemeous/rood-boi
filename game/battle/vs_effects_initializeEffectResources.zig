//! Resets the battle file pointers and curve data, then runs the two setup calls
//! (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: vs_effects_initializeEffectResources (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Clears the battle file pointers and counters, points the curve data and D_800F56C8's pointer field at their tables, then runs the two setup calls.
pub fn vs_effects_initializeEffectResources() callconv(.c) void {
    v.g.vs_effects_resourceReadSlot().* = null;
    v.g.vs_battle_pfileBuf().* = null;
    v.g.vs_effects_textureLoadBuffer().* = null;
    v.g.D_800F5874().* = 0;
    v.g.D_800F5684().* = 0;
    psx.put(&v.g.D_800F57A0().curveBlock, @as(?*v.pFileCurveBlock, null));
    psx.put(&v.g.D_800F57A0().unkD0, v.g.vs_effects_secondaryRequest());
    // ROOD_PC branch: vs_effects_actionRequestPointer is D_800F56C8.unkD0, stored as a guest pointer in the struct.
    psx.put(&v.g.D_800F56C8().unkD0, v.g.vs_effects_actionRequest());
    c.vs_effects_selectEffectResourceContext(0);
    c.vs_effects_bindEffectPackageBlocks(@ptrCast(@alignCast(v.g.vs_effects_emptyPackage())));
}
