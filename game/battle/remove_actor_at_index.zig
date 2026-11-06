//! Removes the battle actor in a slot: its cleanup, its unlink from the actor list and its heap block
//! (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _removeActorAtIndex (src/BATTLE/BATTLE.PRG/146C.c)
/// Removes the actor in slot `index` (below 16) and returns 1, or returns 0 for a slot out of range.
pub fn removeActorAtIndex(index: c_uint, arg1: c_int) callconv(.c) c_int {
    if (index < 16) {
        const slots = v.g.vs_battle_actors();

        if (slots[@as(usize, index)]) |actor| {
            c.func_80069FC4(psx.conv(c_int, index), arg1);

            if (@as(c_int, actor.allegiance) == 0x10) {
                _ = c.func_8009F530(psx.conv(c_int, index));
                _ = c.func_80099D6C(psx.conv(c_int, index));
            } else {
                _ = c.func_8009CFB0(psx.conv(c_int, index));
                if (actor.unk40 == 2) {
                    _ = c.func_8009CFB0(psx.conv(c_int, index +% 4));
                }
            }

            c.func_800E7454(@ptrCast(actor));

            // Empty PS1 slots are skipped: the host has no memory at their NULL pointer. The links are
            // compared and copied as guest addresses, as the C's psx_get/psx_copy do: a stale `next` need
            // not be aligned, and making it a pointer would panic.
            var i: usize = 0;
            while (i < 16) : (i += 1) {
                const slot = slots[i] orelse continue;
                if (psx.rawOf(slot.next) == psx.addr(actor)) {
                    psx.copy(&slot.next, actor.next);
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
