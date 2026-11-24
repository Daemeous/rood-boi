//! Queues a battle object-data load into a stack slot for one actor
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_queueActorWeaponSequenceLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills an object-data request for `index` from the actor's equipped weapon category and populates its slot, when the index and flags allow it.
pub fn vs_actors_queueActorWeaponSequenceLoad(src: [*c]u8, index: c_int, mode: c_int, flags: c_int) callconv(.c) void {
    if ((mode != 0 or index >= 2) and (index < 2 or (flags & 3) == 1)) {
        // The original leaves the other fields as stack residue and populateDataSlot copies the whole record into its slot; zeroed here.
        var sp10: v.vs_battle_objectData = std.mem.zeroes(v.vs_battle_objectData);
        sp10.dataType = 7;
        sp10.index = psx.conv(i8, index);
        const actor: *const v.vs_battle_actor = @ptrCast(@alignCast(src));
        sp10.modelId = if (comptime @hasDecl(c, "vs_mod_animSet")) // Merlose's staves (mod/hooks.h)
            psx.conv(i16, c.vs_mod_animSet(index, actor.equippedWeaponCategory))
        else
            psx.conv(i16, actor.equippedWeaponCategory);
        sp10.actorId = 0;
        _ = c.vs_battle_populateDataSlot(@ptrCast(&sp10));
    }
}
