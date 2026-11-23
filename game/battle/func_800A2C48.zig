//! Battle entity helper: runs the entity's 0x5DC[4] step, clears its state and recurses into the entity at unk17FD (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A2C48 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs the entity's 0x5DC[4] step, clears its state, then recurses into the entity at index unk17FD when that is 2 or more.
pub fn func_800A2C48(entity: ?*v.D_800F4538_t) callconv(.c) void {
    const e = entity.?;
    // &unk5DC[4] is one past the array's end (the address of unk5EC): psx.at does not bounds-check.
    _ = c.vs_actors_translateActorToTileAtRate(e.unk0.slot, @ptrCast(psx.at(&e.unk5DC, 4)), 0);
    e.queuedAnimation = 0;
    psx.setBits(&e.unk0, "unkB_0", 4);
    c.func_800A29A0(@ptrCast(entity));
    if (e.companionSlot >= 2) {
        // The C indexes the 17-entry table with an unchecked u8: psx.at, not a bounds-checked index.
        c.func_800A2C48(@ptrCast(psx.at(v.g.vs_actors_actorRecords(), e.companionSlot).*));
    }
}
