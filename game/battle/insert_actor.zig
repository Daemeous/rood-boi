//! Links a battle actor into the actor list behind slot 0 (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// The 4-byte guest pointer word of a `next` field (psx_raw), read and written as bits.
inline fn rawWord(field: anytype) *align(1) u32 {
    return @ptrCast(field);
}

/// orig: _insertActor (src/BATTLE/BATTLE.PRG/146C.c)
/// Links `actor` in after slot 0 of the battle actor list unless it is already linked; returns 1 when it links.
pub fn insertActor(actor: ?*v.vs_battle_actor) callconv(.c) c_int {
    const a = actor.?;
    if ((@as(c_int, a.allegiance) & 7) != 0 and a.id != 0) {
        const slots = v.g.vs_battle_actors();
        var i: usize = 0;
        while (i < 16) : (i += 1) {
            if (slots[i] != null and psx.get(slots[i].?.next) == actor) {
                return 0;
            }
        }

        // The C repeats the id test above; kept as written.
        if (a.id != 0) {
            // next.raw = vs_battle_actors[0]->next.raw: copies the guest word as bits.
            rawWord(&a.next).* = rawWord(&slots[0].?.next).*;
            rawWord(&slots[0].?.next).* = psx.addr(a);
        } else {
            // psx_set(actor->next, NULL): the guest address 0.
            rawWord(&a.next).* = 0;
        }

        return 1;
    }

    return 0;
}
