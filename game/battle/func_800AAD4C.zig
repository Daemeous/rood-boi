//! Battle actor state slots: sets, clears or toggles one of an actor's two
//! slot states (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800AAD4C (src/BATTLE/BATTLE.PRG/40564.c)
/// Clears (mode 0), sets to `value` (mode 1) or toggles (mode 2) one of an actor's two state slots; -1 for a missing actor or a bad index.
pub fn func_800AAD4C(actorId: c_int, index: c_int, value: c_int, mode: c_int) callconv(.c) c_int {
    // actorId is not range-checked by the C: psx.at indexes as C does.
    const actor = psx.at(v.g.D_800F4538(), actorId).* orelse return -1;
    if (index >= 2) return -1;
    if (mode == 1 and value == 0) return 0;
    // index may be negative (the C does not check it): psx.at wraps as C does.
    const slot = psx.at(&actor.unk180C, index);
    switch (mode) {
        0 => psx.setBits(slot, "unk0_4", 0),
        1 => {
            if (@as(c_int, psx.getBits(slot, "unk0_4")) == 1 and @as(c_int, psx.getBits(slot, "unk0_0")) == value) return 0;
            psx.setBits(slot, "unk0_0", psx.conv(u4, value));
            slot.unk2 = 0;
            if (index == 1) {
                slot.unk3 = psx.conv(u8, actorId *% 10);
            } else {
                slot.unk3 = 0;
            }
            psx.setBits(slot, "unk0_4", psx.conv(u4, mode));
        },
        2 => switch (@as(c_int, psx.getBits(slot, "unk0_4"))) {
            1 => psx.setBits(slot, "unk0_4", 2),
            2 => psx.setBits(slot, "unk0_4", 1),
            else => {},
        },
        else => {},
    }
    return 0;
}
