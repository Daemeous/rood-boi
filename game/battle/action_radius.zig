//! Action range query: the area of effect of an action, or its range from
//! vs_battle_calculateRangeShapeExtent when that lies below a limit (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5600 (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Returns the action's area of effect when its E13 flag is set, else its range from vs_battle_calculateRangeShapeExtent when that is below `arg2`, else 0.
pub fn actionRadius(_: c_int, actionId: c_int, arg2: c_uint) callconv(.c) c_int {
    const action = psx.at(v.g.actions(), actionId);
    var r: c_int = 0;
    if (psx.getBits(action, "flagsE_13") != 0) {
        r = @as(c_int, action.aoe_8) << 5;
    } else if (psx.getBits(action, "flagsE_3") != 0) {
        const vv: c_int = c.vs_battle_calculateRangeShapeExtent(@ptrCast(&action.aoe_0));
        if (psx.conv(c_uint, vv) < arg2) {
            r = vv;
        }
    }
    return r;
}
