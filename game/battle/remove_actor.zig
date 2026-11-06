//! Removes an actor from the battle actor list by relinking its predecessor
//! past it (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _removeActor (src/BATTLE/BATTLE.PRG/146C.c)
/// Unlinks `actor` from the battle actor list: the slot that points at it takes its successor, and it is cleared. Returns 1 if found, else 0.
pub fn removeActor(actor: ?*v.vs_battle_actor) callconv(.c) c_int {
    const a = actor.?;
    if (a.id != 0) {
        const slots = v.g.vs_battle_actors();
        var i: usize = 0;
        while (i < 16) : (i += 1) {
            if (slots[i]) |slot| {
                // Guest addresses compared and copied as the C does: a stale `next` need not be aligned.
                if (psx.rawOf(slot.next) == psx.addr(actor)) {
                    psx.copy(&slot.next, a.next);
                    psx.put(&a.next, null);
                    return 1;
                }
            }
        }
    }

    return 0;
}
