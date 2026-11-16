//! Battle's one-time setup of vs_effects_screenEffectOverlay and its screen effect (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CB79C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Points vs_effects_screenEffectOverlay at D_800F4ED8 when it is still NULL, then runs func_8007E180(6) and _loadScreff2(1).
pub fn func_800CB79C() callconv(.c) void {
    if (psx.get(v.g.vs_effects_screenEffectOverlay().*) == null) {
        const table: *v.D_800EB9B4_t = @ptrCast(@alignCast(v.g.D_800F4ED8()));
        psx.put(v.g.vs_effects_screenEffectOverlay(), table);
        _ = c.func_8007E180(6);
        _ = c._loadScreff2(1);
    }
}
