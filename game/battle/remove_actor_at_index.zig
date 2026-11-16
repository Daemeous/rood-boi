//! Removes the battle actor in a slot: its cleanup, its unlink from the actor list and its heap block
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _removeActorAtIndex (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Removes the actor in slot `index` (below 16) and returns 1, or returns 0 for a slot out of range.
pub fn removeActorAtIndex(index: c_uint, arg1: c_int) callconv(.c) c_int {
    if (index < 16) {
        const slots = v.g.vs_battle_actors();

        if (slots[@as(usize, index)]) |actor| {
            c.vs_actors_rememberEnemy(psx.conv(c_int, index), arg1);

            if (@as(c_int, actor.allegiance) == 0x10) {
                _ = c.vs_field_dropUnsupportedGimmickStack(psx.conv(c_int, index));
                _ = c.vs_field_releaseGimmickInstance(psx.conv(c_int, index));
            } else {
                _ = c.vs_content_releaseActorModel(psx.conv(c_int, index));
                if (actor.unk40 == 2) {
                    _ = c.vs_content_releaseActorModel(psx.conv(c_int, index +% 4));
                }
            }

            c.vs_battle_aiFreeActorAiState(@ptrCast(actor));

            // Empty slots are skipped.
            var i: usize = 0;
            while (i < 16) : (i += 1) {
                const slot = slots[i] orelse continue;
                if (slot.next == actor) {
                    slot.next = actor.next;
                    break;
                }
            }

            c.vs_main_freeHeap(actor);

            slots[@as(usize, index)] = null;
        }

        return 1;
    }

    return 0;
}
