//! Builds the battle target list from the action's target entries and sets the list's position (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_setAllActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the action's target entries into the target list and sets the list's position from its first target.
pub fn vs_battle_setAllActionTargets() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;

    d.unk8.targetCount = d.previewCount;

    var i: c_int = 0;
    while (i < @as(c_int, d.unk8.targetCount)) : (i +%= 1) {
        const src = psx.at(&d.previews, i);
        const dst = psx.at(&d.unk8.targets, i);
        if (@as(c_int, src.unk9) < 0x10) {
            dst.unk40 = 0;
            dst.target.targetActor = src.unk9;
            dst.target.targetLimb = psx.conv(i8, src.unkA);

            if (dst.target.targetLimb == -1) {
                const enemy = v.g.vs_battle_actors()[dst.target.targetActor].?;
                const stats = psx.get(enemy.stats).?;
                dst.target.targetLimb = psx.conv(i8, psx.getBits(stats, "unk36"));
            }
        } else {
            dst.unk40 = 1;
            @as(*align(1) v.SVECTOR, @ptrCast(&dst.target)).* = src.unk0;
            dst.target.mp = psx.conv(i16, src.unk9);
        }
    }

    if (d.unk8.primaryTarget.target.targetActor != 0) {
        const enemy = v.g.vs_battle_actors()[d.unk8.primaryTarget.target.targetActor].?;
        d.unk8.unk844 = @as(*align(1) v.SVECTOR, @ptrCast(&enemy.unk10)).*;
        return;
    }
    d.unk8.unk844 = d.unk2990;
}
