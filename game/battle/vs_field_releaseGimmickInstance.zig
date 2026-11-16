//! Frees a battle object slot: releases its heap block unless another live slot shares its actor, then clears the slot (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_field_releaseGimmickInstance (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Removes battle object slot `arg0`, freeing its heap block when no other slot holds the same actor.
pub fn vs_field_releaseGimmickInstance(arg0: c_int) callconv(.c) c_int {
    if (arg0 >= 16) {
        return -1;
    }

    // A negative arg0 reads before the array, as the C does.
    const temp: *v.D_800F45E0_t = psx.at(v.g.vs_field_gimmickInstances(), arg0).* orelse return -1;

    if (temp.unk0 == 0) {
        return -1;
    }

    var sp10: v.vs_battle_objectData = undefined;
    sp10.index = psx.conv(i8, arg0);
    sp10.actorId = psx.conv(u8, psx.getBits(&temp.unk6C[8], "actorId"));

    var found: bool = false;
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) { // i < 16: the loop bound
        const other = v.g.vs_field_gimmickInstances()[@intCast(i)];
        if (@as(c_int, sp10.index) != i and other != null and
            @as(c_int, psx.getBits(&other.?.unk6C[8], "actorId")) == @as(c_int, sp10.actorId))
        {
            sp10.dataAddr = psx.conv(u32, i);
            found = true;
            break;
        }
    }

    if (!found) {
        c.vs_main_freeHeap(psx.get(temp.unk68));
    }

    c.vs_battle_aiRemoveAiTileOccupant(@ptrCast(temp));
    psx.at(v.g.vs_field_gimmickInstances(), arg0).* = null;
    return 0;
}
