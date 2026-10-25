//! The text box string setter (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_setTextBox (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Points text box `boxId` at `str` and restarts it; with a null `str`, reports whether the box is still busy.
pub fn setTextBox(boxId: c_int, str: [*c]u8) callconv(.c) c_int {
    const box = psx.at(v.g.textBoxes(), boxId);

    if (str != null) {
        psx.put(&box.string, str);
        box.speed = 1;
        box.unk4 = 0;
        box.prevChunksPrinted = 0;
        if ((@as(c_int, psx.getBits(&box.unk0, "unk0_24")) & 0x20) == 0) {
            _ = c.func_800AAD4C(
                @as(c_int, psx.getBits(&box.unk0, "unk0_24")),
                0,
                @as(c_int, box.unk2E) + 1,
                1,
            );
            box.unk2C = 1;
        }
        return 0;
    }

    if (box.speed != 0xFFFF and box.speed != 0) {
        return -1;
    }

    return 0;
}
