//! Room section 12 lookup: the value of entry `arg0` when its flag is set
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_movingPlatformDwellTicks (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns field unk4_16 of room section 12 entry `arg0` when its unk4_0 flag is 1, else -1.
pub fn vs_field_movingPlatformDwellTicks(arg0: c_int) callconv(.c) c_uint {
    if (psx.get(v.g.vs_battle_roomData().section12)) |sections| {
        const entry = psx.at(sections, arg0);
        if (psx.getBits(entry, "unk4_0") == 1) {
            return psx.conv(c_uint, psx.getBits(entry, "unk4_16"));
        }
    }
    return 0xFFFF_FFFF;
}
