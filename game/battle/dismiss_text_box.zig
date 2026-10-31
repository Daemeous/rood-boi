//! Text box dismissal: closes box `id` and reports whether it is still busy (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_dismissTextBox (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs box `id`'s close effect unless flag 0x20 is set, then ends its open or closing state; returns -1 while it is busy.
pub fn dismissTextBox(id: c_int) callconv(.c) c_int {
    const box = psx.at(v.g.textBoxes(), id);

    const flags: c_int = @as(c_int, psx.getBits(&box.unk0, "unk0_24"));
    if ((flags & 0x20) == 0) {
        _ = c.func_800AAD4C(flags, 0, 0, 0);
    }

    if (box.state != 0) {
        if (box.state > 0) {
            box.unk22 = 8;
            box.state = -1;
            psx.setBits(&box.unk0, "unk0_4", 0);
        }
        return -1;
    }

    return 0;
}
