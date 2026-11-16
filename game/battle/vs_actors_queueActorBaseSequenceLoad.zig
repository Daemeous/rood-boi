//! Fills a battle object-data record for data type 7 and populates a data slot with it
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_queueActorBaseSequenceLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Populates a data slot for object type 7 at `index`; the record's other fields are left as the C leaves them.
pub fn vs_actors_queueActorBaseSequenceLoad(_: ?*v.vs_battle_actor, index: c_int) callconv(.c) void {
    // The C leaves sp10's other fields uninitialised; they are passed on as they are.
    var sp10: v.vs_battle_objectData = undefined;
    sp10.dataType = 7;
    sp10.index = psx.conv(i8, index);
    sp10.modelId = 0;
    sp10.actorId = 0;
    _ = c.vs_battle_populateDataSlot(@ptrCast(&sp10));
}
