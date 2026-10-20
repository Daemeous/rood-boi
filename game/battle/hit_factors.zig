//! A hit's stat factors (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _factorDamageReceived (src/BATTLE/BATTLE.PRG/146C.c)
/// The current action's received hp + mp times hit `hitNumber`'s stat factor, over 10, when it took HP damage.
pub fn factorDamageReceived(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, hitNumber: c_int, _: c_int) callconv(.c) i16 {
    var factor: i16 = 0;
    const block = v.g.D_800F19CC().*.?;
    if (@as(c_int, psx.getBits(&block.unk8.unk4, "hpEffect")) == 1 or @as(c_int, psx.getBits(&block.unk8.unk4, "hpEffect")) == 3) {
        const received: c_int = @as(c_int, block.unk8.unk4.unk0.hp) +% block.unk8.unk4.unk0.mp;
        // hitParams[hitNumber]: the C does not check the hit number.
        const statFactor: c_int = psx.getBits(psx.at(&action.?.hitParams, hitNumber), "statFactor");
        factor = psx.conv(i16, @divTrunc(received *% statFactor, 10));
    }
    return factor;
}
