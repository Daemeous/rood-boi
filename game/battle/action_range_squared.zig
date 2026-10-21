//! Reach of an action's range, from the action's own range or the actor's (src/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5698 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Squared reach of an action's range (the actor's own range when rangeX is 0xFF), less 32 above 32.
pub fn actionRangeSquared(actor: ?*v.func_800E5698_t, actionId: c_int) callconv(.c) c_int {
    const action = psx.at(v.g.actions(), actionId);
    const range: *v.func_800E5568_t = if (action.rangeX == 0xFF)
        &psx.get(actor.?.unk5C).?.unk38
    else
        @ptrCast(&action.rangeX);

    var reach: c_uint = psx.conv(c_uint, c.func_800E5568(@ptrCast(range)));
    if (reach > 0x20) {
        reach -%= 0x20;
    }
    return psx.conv(c_int, reach *% reach);
}
