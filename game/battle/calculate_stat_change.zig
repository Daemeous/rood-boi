//! Stat change of one hit of an action on its target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

const Calculator = *const fn (?*v.vs_action_t, ?*v._hitEntity_t, ?*v._hitEntity_t, c_int, c_int) callconv(.c) c_short;

/// orig: _calculateStatChange (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Works out the stat change of hit `hit` of `action` on `arg2` (999 against unk957 == 128) and records it in _lastValue.
pub fn calculateStatChange(action: ?*v.vs_action_t, arg1: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_short {
    var amount: c_short = undefined;
    const target = arg2.?;

    // vs_battle_actors[targetActor]: the index is a u8 and the table has no bounds in C.
    if (target.unk40 == 0 and psx.get(psx.at(v.g.vs_battle_actors(), target.target.targetActor).*.?.stats).?.actorId == 128) {
        amount = 999;
    } else {
        // action->hitParams[hit]: hit is not checked against the array in C.
        const params = psx.at(&action.?.hitParams, hit);
        const entry = psx.at(v.g._statCalculators(), psx.getBits(params, "statCalculator"));
        const calc: Calculator = @ptrCast(entry.*.?);
        amount = calc(action, arg1, arg2, hit, arg4);
    }

    v.g._lastValue().* = psx.conv(u32, amount);
    return amount;
}
