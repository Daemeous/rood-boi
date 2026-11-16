//! Builds the battle target list from the action's target entries and sets the list's position (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_setAllActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the action's target entries into the target list and sets the list's position from its first target.
pub fn vs_battle_setAllActionTargets() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;

    d.unk8.unk4A = d.unk2C00;

    var i: c_int = 0;
    while (i < @as(c_int, d.unk8.unk4A)) : (i +%= 1) {
        const src = psx.at(&d.unk29C0, i);
        const dst = psx.at(&d.unk8.unk4C, i);
        if (@as(c_int, src.unk9) < 0x10) {
            dst.unk40 = 0;
            dst.unk0.targetActor = src.unk9;
            dst.unk0.targetLimb = psx.conv(i8, src.unkA);

            if (dst.unk0.targetLimb == -1) {
                const enemy = v.g.vs_battle_actors()[dst.unk0.targetActor].?;
                const stats = psx.get(enemy.stats).?;
                dst.unk0.targetLimb = psx.conv(i8, psx.getBits(stats, "unk36"));
            }
        } else {
            dst.unk40 = 1;
            @as(*align(1) v.SVECTOR, @ptrCast(&dst.unk0)).* = src.unk0;
            dst.unk0.mp = psx.conv(i16, src.unk9);
        }
    }

    if (d.unk8.unk4.unk0.targetActor != 0) {
        const enemy = v.g.vs_battle_actors()[d.unk8.unk4.unk0.targetActor].?;
        d.unk8.unk844 = @as(*align(1) v.SVECTOR, @ptrCast(&enemy.unk10)).*;
        return;
    }
    d.unk8.unk844 = d.unk2990;
}
