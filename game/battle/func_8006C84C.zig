//! Dispatches each queued entry of the current battle slot to its action's
//! callback and runs the per-entry tick (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006C84C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the queued entries of slot `unk0 & 3` through their action callbacks, ticking after each.
pub fn func_8006C84C(arg0: c_int) callconv(.c) c_int {
    const gs = v.g.vs_battle_actionState().*.?;
    const temp = &gs.queue[@as(usize, gs.unk0 & 3)];

    var i: c_int = 0;
    while (i < @as(c_int, temp.targetCount)) : (i +%= 1) {
        // The C does not check i against the 30 entries; psx.at keeps its address arithmetic.
        const entry = psx.at(&temp.targets, i);

        if (entry.unk40 == 0) {
            if (@as(c_int, temp.actionIndex) < 0x28) {
                // actionIndex < 0x28 (checked above): a valid action slot.
                const index: usize = @as(usize, temp.actionIndex);
                if (psx.getBits(&v.g.actions()[index].hitParams[0], "prerequisiteFunction") == 3) {
                    c.func_8006C4C4(@as(c_int, temp.actionIndex), @ptrCast(entry), @as(c_int, entry.target.unk3));
                } else {
                    const kind: c_int = entry.target.unk3;
                    switch (kind) {
                        0, 2, 5 => {
                            if (@as(c_int, temp.actionIndex) < 0x16) {
                                c.func_8006C4C4(1, @ptrCast(entry), kind);
                            } else {
                                c.func_8006C4C4(@as(c_int, temp.actionIndex), @ptrCast(entry), kind);
                            }
                        },
                        1 => c.func_8006C4C4(0x27, @ptrCast(entry), kind),
                        3, 4, 6 => c.func_8006C4C4(0x26, @ptrCast(entry), kind),
                        else => {},
                    }
                }
            } else {
                c.func_8006C4C4(@as(c_int, temp.actionIndex), @ptrCast(entry), @as(c_int, entry.target.unk3));
            }
        } else {
            c.func_8006C4C4(0x26, @ptrCast(entry), 0);
        }

        _ = c.vs_battle_playWeaponHitSounds(arg0);
    }

    return 1;
}
