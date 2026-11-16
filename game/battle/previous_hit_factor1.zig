//! Previous-hit factor for a hit (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _previousHitFactor1 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the first character's HP plus MP by the action's first hit stat factor, plus the field's base value less one.
pub fn previousHitFactor1(arg0: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    const player: *v.D_800F19CC_t = v.g.vs_battle_actionState().*.?;
    const entry = &player.unk8.targets[0];

    // Both operands promote to int; the product wraps as in C.
    const hpSum: c_int = @as(c_int, entry.target.hp) +% @as(c_int, entry.target.mp);
    // arg3 is not checked against the array bound by the original.
    const statFactor: c_int = psx.getBits(psx.at(&arg0.?.hitParams, arg3), "statFactor");
    const scaled: c_int = @divTrunc(hpSum *% statFactor, 10);

    // The sum is unsigned (unk0 is u32), so it wraps in unsigned arithmetic before the short conversion.
    const sum: c_uint = psx.conv(c_uint, scaled) +% (player.unk0 -% 1);
    return psx.conv(c_short, sum);
}
