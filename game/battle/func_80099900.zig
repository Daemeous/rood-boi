//! Finds another battle object slot that shares this object's actor id (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_80099900 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Finds the first other slot whose sub-record 8 has this object's actor id, stores that slot in dataAddr and returns 1; else 0.
pub fn func_80099900(object: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const data = object.?;
    const slots = v.g.D_800F45E0();
    var i: c_int = 0;
    // i < 16: the loop bound, so the increment cannot overflow.
    while (i < 16) : (i +%= 1) {
        const slot = slots[@intCast(i)];
        if (i != @as(c_int, data.index) and
            slot != null and
            @as(c_int, psx.getBits(&slot.?.unk6C[8], "actorId")) == @as(c_int, data.actorId))
        {
            data.dataAddr = psx.conv(u32, i);
            return 1;
        }
    }
    return 0;
}
