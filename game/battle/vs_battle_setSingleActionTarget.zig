//! Battle action targeting: copies the queued action's target into the first target slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_setSingleActionTarget (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the first target of the current action from its queued entry and refreshes the action's position.
pub fn vs_battle_setSingleActionTarget() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;
    d.unk8.unk4A = 1;
    // unk298C is a u8 index into 23 entries; the C does not check it, so psx.at is used.
    const entry = psx.at(&d.unk29C0, d.unk298C);
    const temp: u8 = entry.unk9;
    if (temp < 16) {
        d.unk8.unk4C[0].unk40 = 0;
        d.unk8.unk4C[0].unk0.targetActor = entry.unk9;
        d.unk8.unk4C[0].unk0.targetLimb = psx.conv(i8, entry.unkA);
        if (d.unk8.unk4C[0].unk0.targetLimb == -1) {
            const actor = v.g.vs_battle_actors()[@as(usize, d.unk8.unk4C[0].unk0.targetActor)].?;
            const stats = psx.get(actor.stats).?;
            d.unk8.unk4C[0].unk0.targetLimb = psx.conv(i8, psx.getBits(stats, "unk36"));
        }
    } else {
        d.unk8.unk4C[0].unk40 = if ((temp & 0x10) != 0) 4 else 1;
        // *(SVECTOR*)&unk4C[0].unk0 = entry.unk0: an 8-byte copy into an align(2) field.
        const dst: *align(1) v.SVECTOR = @ptrCast(&d.unk8.unk4C[0].unk0);
        dst.* = entry.unk0;
        d.unk8.unk4C[0].unk0.mp = psx.conv(i16, entry.unk9);
    }
    if (d.unk8.unk4.unk40 == 0) {
        if (entry.unk9 == d.unk8.unk4.unk0.targetActor) {
            _ = c.vs_battle_aimActionBehindSource(@ptrCast(&d.unk8));
            return;
        }
        d.unk8.unk844 = entry.unk0;
        return;
    }
    d.unk8.unk844 = entry.unk0;
}
