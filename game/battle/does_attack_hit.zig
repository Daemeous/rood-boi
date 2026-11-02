//! Whether an attack's hit lands (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

const HitFunction = *const fn (?*v.vs_action_t, ?*v._hitEntity_t, ?*v._hitEntity_t, c_int, c_int) callconv(.c) c_int;

/// orig: _doesAttackHit (src/BATTLE/BATTLE.PRG/146C.c)
/// The hit chance is 100 against an actor whose unk957 is 0x80, else the hit's _hitFunctions entry (0 for a spell
/// on an actor with the top status bit); D_800F1A00 0 makes it 255 (sure). Below 255 it doubles on status 4 and
/// caps at 100. The first hit, or one with no threshold yet (255), records it; then 1 when vs_main_getRand(100)
/// falls under it (+10 for the player's own attack), 0 for a sure hit.
pub fn doesAttackHit(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitNumber: c_int, addVariance: c_int) callconv(.c) c_int {
    const actors = v.g.vs_battle_actors();
    var threshold: c_int = undefined;
    if (target.?.unk40 == 0 and psx.get(actors[target.?.unk0.targetActor].?.stats).?.unk957 == 0x80) {
        threshold = 100;
    } else {
        const table = psx.globalMany(?HitFunction, "_hitFunctions");
        // hitParams has 2 entries; hitNumber is not bounded by the C.
        const params = psx.at(&action.?.hitParams, hitNumber);
        threshold = table[psx.getBits(params, "hitFunction")].?(action, source, target, hitNumber, addVariance);
        if (@as(c_int, psx.getBits(action.?, "type")) == c.actionTypeSpell and
            psx.get(actors[target.?.unk0.targetActor].?.stats).?.statuses < 0)
        {
            threshold = 0;
        }
    }
    if (v.g.D_800F1A00().* == 0) {
        threshold = 255;
    }
    if (threshold != 255) {
        if (target.?.unk40 == 0) {
            if (psx.get(actors[target.?.unk0.targetActor].?.stats).?.statuses & 4 != 0) {
                threshold *%= 2;
            }
        }
        if (threshold > 100) {
            threshold = 100;
        }
    }
    if (hitNumber == 0 or target.?.unk0.hitThreshold == 255) {
        target.?.unk0.hitThreshold = psx.conv(u8, threshold);
    }
    const chance = c.vs_main_getRand(100);
    if (threshold != 255) {
        if (source.?.unk40 == 0 and source.?.unk0.targetActor == 0) {
            threshold +%= 10;
        }
        return @intFromBool(chance < threshold);
    }
    return 0;
}
