//! Links each battle actor whose kind is 0x10 to the first actor whose id matches its
//! linked field (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");

/// orig: func_800E5998 (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Sets the link of each 0x10-kind actor's slot to the id of the first actor whose unk30_0 matches its unk30_9.
pub fn func_800E5998() callconv(.c) void {
    var actor = psx.get(v.g.vs_battle_actors()[0].?.next);
    while (actor) |a| {
        if (@as(c_int, psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*.?.target16) == 0x10) {
            var other = psx.get(v.g.vs_battle_actors()[0].?.next);
            while (other) |o| {
                if (@as(c_int, psx.getBits(&o.flags30.bits, "unk30_0")) == @as(c_int, psx.getBits(&a.flags30.bits, "unk30_9"))) {
                    break;
                }
                other = psx.get(o.next);
            }
            if (other) |o| {
                psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*.?.target16 = psx.conv(i8, o.id);
            }
        }
        actor = psx.get(a.next);
    }
}
