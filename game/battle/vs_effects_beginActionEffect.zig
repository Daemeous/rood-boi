//! Copies a battle record into vs_effects_actionRequest and runs the battle-start setup
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_beginActionEffect (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores a record, keeps its id and flag when the record's kind is 4, runs the setup calls and returns the status byte.
pub fn vs_effects_beginActionEffect(record: ?*v.func_800CF0E8_t, id: c_int, flag: c_int) callconv(.c) c_int {
    v.g.vs_effects_actionRequest().* = record.?.*;

    if (id != 0 and record.?.unk4 == 4) {
        v.g.D_800F522C().* = psx.conv(u8, id);
        v.g.D_800F54A9().* = psx.conv(u8, flag);
    }

    v.g.vs_effects_primaryToken().* = psx.conv(u8, c.vs_effects_allocateEffectToken());
    c.vs_effects_selectEffectResourceContext(1);
    c.vs_effects_captureEffectCamera(psx.cptr(c.D_800F54D8_t, v.g.vs_effects_savedActionCamera()));
    c.vs_effects_captureEffectCamera(psx.cptr(c.D_800F54D8_t, v.g.vs_effects_effectCamera()));
    c.vs_effects_setPrimaryEffectPhase(6);

    return @as(c_int, v.g.vs_effects_primaryToken().*);
}
