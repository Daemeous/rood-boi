//! Finds another weapon model slot that shares the object's model id (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_80099FA8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Points the object's data address at the first other weapon model slot with its model id and returns 1, else 0.
pub fn func_80099FA8(objData: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const data = objData.?;
    const models = v.g.vs_battle_wepModels();
    const index: c_int = @as(c_int, data.index);

    var i: c_int = 0;
    while (i < 20) : (i += 1) {
        if (i != index) {
            if (models[@intCast(i)]) |model| {
                if ((psx.getBits(model, "actorId") != 0 or index < 4) and
                    @as(c_int, model.modelId) == @as(c_int, data.modelId))
                {
                    data.dataAddr = psx.conv(u32, i);
                    return 1;
                }
            }
        }
    }
    return 0;
}
