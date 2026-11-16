//! Fills a battle object-data record (type 7, the weapon model) and hands it to the populate call (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_queueSharedSequenceWithTextures (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Populates a weapon model data slot for `index` with model `wepId`.
pub fn vs_actors_queueSharedSequenceWithTextures(index: c_int, wepId: c_int, arg2: c_int) callconv(.c) void {
    // Fields other than the four set here are uninitialised in the original.
    var objData: v.vs_battle_objectData = undefined;
    objData.dataType = 7;
    objData.index = psx.conv(i8, index);
    objData.modelId = psx.conv(i16, wepId);
    objData.actorId = 2;
    objData.unk11 = psx.conv(u8, arg2);
    _ = c.vs_battle_populateDataSlot(@ptrCast(&objData));
}
