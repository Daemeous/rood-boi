//! Finds another battle object slot holding the same model and links to it (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: vs_content_findSharedActorModel (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Looks for another battle object slot with this object's model; on a match stores that slot in dataAddr and returns 1, else 0.
pub fn vs_content_findSharedActorModel(arg0: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const obj = arg0.?;
    const table = v.g.vs_actors_actorRecords();
    var i: c_int = 0;
    while (i < 17) : (i += 1) {
        if (i != @as(c_int, obj.index)) {
            const entry = table[@intCast(i)];
            if (entry != null and @as(c_int, entry.?.unk6E6) == @as(c_int, obj.modelId)) {
                obj.dataAddr = psx.conv(u32, i);
                return 1;
            }
        }
    }
    return 0;
}
