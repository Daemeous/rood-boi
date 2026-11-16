//! Toggles the per-entry flag byte of vs_field_roomEffectInstances for an index below vs_field_textureEffectCount or in [0x40, vs_field_roomEffectScriptCount + 0x40) (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: vs_field_toggleRoomEffectUpdates (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Flips the flag byte of entry `arg0` when it is in one of two index ranges; returns 1 if it did, else 0.
pub fn vs_field_toggleRoomEffectUpdates(arg0: c_int) callconv(.c) c_int {
    if (arg0 < @as(c_int, v.g.vs_field_textureEffectCount().*)) {
        // arg0 may be negative here: psx.at indexes without a bounds check, as the C does.
        const entry = psx.at(v.g.vs_field_roomEffectInstances(), arg0);
        entry.unk2 = @as(u8, @intFromBool(entry.unk2 < 1));
        return 1;
    }
    if ((arg0 >= 0x40) and (arg0 < @as(c_int, v.g.vs_field_roomEffectScriptCount().*) +% 0x40)) {
        const entry = psx.at(v.g.vs_field_roomEffectInstances(), arg0);
        entry.unk2 = @as(u8, @intFromBool(entry.unk2 < 1));
        return 1;
    }
    return 0;
}
