//! Selects which data block set the 6F014 overlay uses (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: vs_effects_selectEffectResourceContext (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Points vs_effects_package at D_800F57A0 when `which` is 0, else at D_800F56C8.
pub fn vs_effects_selectEffectResourceContext(which: c_int) callconv(.c) void {
    if (which == 0) {
        psx.put(v.g.vs_effects_package(), v.g.D_800F57A0());
    } else {
        psx.put(v.g.vs_effects_package(), v.g.D_800F56C8());
    }
}
